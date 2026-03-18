#include "playback_controller.h"
#include "../qt/ffmpeg_video_buffer.h"

#include <QVideoSink>
#include <QVideoFrame>

extern "C" {
#include <libavutil/mathematics.h>
}

namespace videoengine {

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent)
{
    displayTimer_.setTimerType(Qt::PreciseTimer);
    connect(&displayTimer_, &QTimer::timeout,
            this, &PlaybackController::onDisplayTimer);
}

PlaybackController::~PlaybackController()
{
    stop();
}

void PlaybackController::init(VideoSource* source, VideoDecoder* decoder,
                              const KeyframeIndex* index)
{
    source_ = source;
    decoder_ = decoder;
    keyframeIndex_ = index;
    timeBase_ = source->streamInfo().timeBase;
}

// --- Playback control ---

void PlaybackController::play()
{
    if (running_ && paused_) {
        // Resume from pause: threads are alive, just wake them
        paused_ = false;
        pauseCv_.notify_all();
        wallStart_ = std::chrono::steady_clock::now();
        ptsStartUs_ = currentPositionUs_.load();
        displayTimer_.start(1);
        return;
    }

    if (running_) {
        return; // already playing
    }

    // Starting fresh (after stop, seek, or step — no threads running)
    // Source and decoder are at the correct position from the last operation.
    wallStart_ = std::chrono::steady_clock::now();
    ptsStartUs_ = currentPositionUs_.load();
    lastDisplayedPtsUs_ = currentPositionUs_.load() - 1; // force display of current frame
    startThreads();
    displayTimer_.start(1);
}

void PlaybackController::pause()
{
    if (!running_ || paused_) return;
    paused_ = true;
    displayTimer_.stop();
}

void PlaybackController::stop()
{
    displayTimer_.stop();
    stopThreads();
    frameBuffer_.clear();
    currentPositionUs_ = 0;
    lastDisplayedPtsUs_ = -1;
}

// --- Seeking ---

void PlaybackController::seekTo(int64_t targetUs)
{
    bool wasPlaying = isPlaying();
    stopThreads();
    displayTimer_.stop();

    // Clamp to valid range
    int64_t durationUs = source_->streamInfo().durationUs;
    if (durationUs > 0) {
        targetUs = std::clamp(targetUs, int64_t{0}, durationUs);
    }

    frameBuffer_.clear();

    // Find nearest keyframe at or before the target
    if (keyframeIndex_ && !keyframeIndex_->empty()) {
        auto kf = keyframeIndex_->nearestBefore(targetUs);
        if (kf) {
            source_->seekTo(kf->ptsUs);
        }
    } else {
        source_->seekTo(targetUs);
    }
    decoder_->reset();

    // Decode forward to the target frame, caching all intermediate frames
    int64_t resultPtsUs = decodeForwardTo(targetUs);

    if (resultPtsUs >= 0) {
        currentPositionUs_ = resultPtsUs;
        lastDisplayedPtsUs_ = resultPtsUs;

        // Display the target frame
        auto tf = frameBuffer_.frameBefore(resultPtsUs);
        if (tf) {
            displayFrame(tf->frame);
        }
    }

    if (wasPlaying) {
        wallStart_ = std::chrono::steady_clock::now();
        ptsStartUs_ = currentPositionUs_.load();
        startThreads();
        displayTimer_.start(1);
    }
}

// --- Stepping ---

bool PlaybackController::stepForward()
{
    // Stop threads if they're running (e.g. paused with threads alive)
    if (running_) {
        stopThreads();
        displayTimer_.stop();
    }

    int64_t currentUs = currentPositionUs_.load();

    // Check FrameBuffer cache first
    auto cached = frameBuffer_.frameAfter(currentUs);
    if (cached) {
        displayFrame(cached->frame);
        currentPositionUs_ = cached->ptsUs;
        lastDisplayedPtsUs_ = cached->ptsUs;
        return true;
    }

    // Cache miss — decode one frame from the source
    int64_t nextUs = decodeForwardTo(currentUs + 1); // +1 ensures we get the NEXT frame
    if (nextUs < 0) {
        return false; // EOF
    }

    auto frame = frameBuffer_.frameBefore(nextUs);
    if (frame) {
        displayFrame(frame->frame);
        currentPositionUs_ = frame->ptsUs;
        lastDisplayedPtsUs_ = frame->ptsUs;
        return true;
    }
    return false;
}

bool PlaybackController::stepBackward()
{
    if (running_) {
        stopThreads();
        displayTimer_.stop();
    }

    int64_t currentUs = currentPositionUs_.load();
    if (currentUs <= 0) return false;

    // Check FrameBuffer cache for the frame before current
    // We need strictly less than current, so subtract 1µs
    auto cached = frameBuffer_.frameBefore(currentUs - 1);
    if (cached) {
        displayFrame(cached->frame);
        currentPositionUs_ = cached->ptsUs;
        lastDisplayedPtsUs_ = cached->ptsUs;
        return true;
    }

    // Cache miss — need to seek to keyframe and decode forward
    int64_t targetUs = currentUs - 1; // one µs before current
    if (keyframeIndex_ && !keyframeIndex_->empty()) {
        auto kf = keyframeIndex_->nearestBefore(targetUs);
        if (kf) {
            source_->seekTo(kf->ptsUs);
        }
    } else {
        source_->seekTo(targetUs);
    }
    decoder_->reset();
    frameBuffer_.clear();

    // Decode forward to the frame just before currentUs
    int64_t resultUs = decodeForwardTo(targetUs);
    if (resultUs >= 0) {
        auto frame = frameBuffer_.frameBefore(currentUs - 1);
        if (frame) {
            displayFrame(frame->frame);
            currentPositionUs_ = frame->ptsUs;
            lastDisplayedPtsUs_ = frame->ptsUs;
            return true;
        }
    }
    return false;
}

// --- Sink & queries ---

void PlaybackController::connectToSink(QVideoSink* sink)
{
    sink_ = sink;
}

int64_t PlaybackController::positionUs() const
{
    return currentPositionUs_.load();
}

bool PlaybackController::isPlaying() const
{
    return running_ && !paused_;
}

bool PlaybackController::isPaused() const
{
    return running_ && paused_;
}

// --- Thread management ---

void PlaybackController::startThreads()
{
    running_ = true;
    paused_ = false;
    packetQueue_.reset();
    sourceThread_ = std::thread(&PlaybackController::sourceLoop, this);
    decodeThread_ = std::thread(&PlaybackController::decodeLoop, this);
}

void PlaybackController::stopThreads()
{
    running_ = false;
    paused_ = false;
    pauseCv_.notify_all();
    packetQueue_.shutdown();

    if (sourceThread_.joinable()) sourceThread_.join();
    if (decodeThread_.joinable()) decodeThread_.join();
}

void PlaybackController::sourceLoop()
{
    while (running_) {
        {
            std::unique_lock lock(pauseMutex_);
            pauseCv_.wait(lock, [this] { return !paused_ || !running_; });
        }
        if (!running_) break;

        auto pkt = source_->readPacket();
        if (!pkt) {
            packetQueue_.shutdown();
            break;
        }
        if (!packetQueue_.push(std::move(*pkt))) break;
    }
}

void PlaybackController::decodeLoop()
{
    while (running_) {
        {
            std::unique_lock lock(pauseMutex_);
            pauseCv_.wait(lock, [this] { return !paused_ || !running_; });
        }
        if (!running_) break;

        auto pkt = packetQueue_.pop();
        if (!pkt) break;

        auto frame = decoder_->decode(pkt->raw());
        if (frame && frame->isValid()) {
            int64_t ptsUs = ptsToUs(frame->pts());
            frameBuffer_.push(std::move(*frame), ptsUs);
        }
    }

    // Drain remaining frames from the decoder after EOF
    while (running_) {
        auto frame = decoder_->flush();
        if (!frame || !frame->isValid()) break;
        int64_t ptsUs = ptsToUs(frame->pts());
        frameBuffer_.push(std::move(*frame), ptsUs);
    }
}

void PlaybackController::onDisplayTimer()
{
    if (!sink_ || paused_) return;

    auto now = std::chrono::steady_clock::now();
    int64_t elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        now - wallStart_).count();
    int64_t targetPtsUs = ptsStartUs_ + elapsedUs;

    // Advance through frames that should have been displayed by now
    bool frameChanged = false;
    DecodedFrame latestFrame;

    while (true) {
        auto entry = frameBuffer_.frameAfter(lastDisplayedPtsUs_);
        if (!entry || entry->ptsUs > targetPtsUs) break;

        latestFrame = std::move(entry->frame);
        lastDisplayedPtsUs_ = entry->ptsUs;
        currentPositionUs_ = entry->ptsUs;
        frameChanged = true;
    }

    if (frameChanged) {
        displayFrame(latestFrame);
    }

    // Check end of stream
    if (!running_ && !frameChanged && frameBuffer_.empty()) {
        displayTimer_.stop();
        emit endOfStream();
    }
}

// --- Helpers ---

void PlaybackController::displayFrame(const DecodedFrame& frame)
{
    if (!sink_ || !frame.isValid()) return;
    auto buffer = std::make_unique<FFmpegVideoBuffer>(DecodedFrame(frame));
    QVideoFrame videoFrame(std::move(buffer));
    sink_->setVideoFrame(videoFrame);
}

int64_t PlaybackController::decodeForwardTo(int64_t targetUs)
{
    // Decode packets until we get a frame with PTS >= targetUs.
    // All decoded frames are cached in the FrameBuffer.
    // Returns the PTS of the frame found, or -1 on failure.
    int64_t bestPtsUs = -1;

    for (int safety = 0; safety < 500; ++safety) {
        auto pkt = source_->readPacket();
        if (!pkt) break;

        auto frame = decoder_->decode(pkt->raw());
        if (frame && frame->isValid()) {
            int64_t ptsUs = ptsToUs(frame->pts());
            frameBuffer_.push(std::move(*frame), ptsUs);
            bestPtsUs = ptsUs;

            if (ptsUs >= targetUs) {
                return ptsUs;
            }
        }
    }

    // Didn't reach target — return best we got (or -1)
    return bestPtsUs;
}

int64_t PlaybackController::ptsToUs(int64_t pts) const
{
    return av_rescale_q(pts, timeBase_, {1, 1'000'000});
}

} // namespace videoengine
