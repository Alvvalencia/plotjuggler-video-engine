#ifndef VIDEOENGINE_CORE_PLAYBACK_CONTROLLER_H
#define VIDEOENGINE_CORE_PLAYBACK_CONTROLLER_H

#include "packet_queue.h"
#include "frame_buffer.h"
#include "keyframe_index.h"
#include "video_source.h"
#include "video_decoder.h"
#include <QObject>
#include <QTimer>
#include <atomic>
#include <thread>
#include <chrono>

class QVideoSink;

namespace videoengine {

// Drives the 3-thread playback pipeline:
//   Source thread:  readPacket() → PacketQueue
//   Decode thread:  PacketQueue → decode() → FrameBuffer
//   UI timer:       FrameBuffer → QVideoSink (at correct PTS)
//
// Seeking/stepping stop threads, do synchronous work, then optionally
// restart. This keeps the source/decoder ownership model simple.
class PlaybackController : public QObject {
    Q_OBJECT
public:
    explicit PlaybackController(QObject* parent = nullptr);
    ~PlaybackController() override;

    // Initialize pipeline components (does not take ownership).
    void init(VideoSource* source, VideoDecoder* decoder,
              const KeyframeIndex* index);

    void play();
    void pause();
    void stop();

    // Frame-accurate seek: finds nearest keyframe, decodes forward to target.
    // If playing, resumes after seek. If paused, stays paused.
    void seekTo(int64_t targetUs);

    // Step one frame forward/backward from current position.
    // Must not be playing (call pause first). Returns false at EOF/BOF.
    bool stepForward();
    bool stepBackward();

    void connectToSink(QVideoSink* sink);
    int64_t positionUs() const;
    bool isPlaying() const;
    bool isPaused() const;

signals:
    void endOfStream();

private:
    void startThreads();
    void stopThreads();
    void sourceLoop();
    void decodeLoop();
    void onDisplayTimer();

    // Push a frame directly to the video sink (for seek/step display).
    void displayFrame(const DecodedFrame& frame);

    // Decode forward from current source position until a frame with
    // PTS >= targetUs is found. Caches all intermediate frames.
    // Returns the target frame's PTS in µs, or -1 on failure.
    int64_t decodeForwardTo(int64_t targetUs);

    // Convert raw PTS (stream time_base units) to microseconds.
    int64_t ptsToUs(int64_t pts) const;

    VideoSource* source_ = nullptr;
    VideoDecoder* decoder_ = nullptr;
    const KeyframeIndex* keyframeIndex_ = nullptr;
    QVideoSink* sink_ = nullptr;
    QTimer displayTimer_;

    PacketQueue packetQueue_{64};
    FrameBuffer frameBuffer_{256 * 1024 * 1024}; // 256 MB

    // Playback clock
    std::chrono::steady_clock::time_point wallStart_;
    int64_t ptsStartUs_ = 0;
    std::atomic<int64_t> currentPositionUs_{0};
    int64_t lastDisplayedPtsUs_ = -1;

    // Thread management
    std::thread sourceThread_;
    std::thread decodeThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::mutex pauseMutex_;
    std::condition_variable pauseCv_;

    AVRational timeBase_{0, 1};
};

} // namespace videoengine

#endif // VIDEOENGINE_CORE_PLAYBACK_CONTROLLER_H
