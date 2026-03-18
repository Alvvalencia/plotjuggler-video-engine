#include "FFmpegBackend.h"

namespace videoengine {

FFmpegBackend::FFmpegBackend(QObject* parent)
    : QObject(parent)
    , controller_(std::make_unique<PlaybackController>(this))
{
}

FFmpegBackend::~FFmpegBackend()
{
    stop();
}

bool FFmpegBackend::open(const QString& path)
{
    stop();

    if (!source_.open(path.toStdString())) {
        state_ = PlaybackState::ERROR;
        return false;
    }

    if (!decoder_.open(source_.codecParameters())) {
        source_.close();
        state_ = PlaybackState::ERROR;
        return false;
    }

    durationUs_ = source_.streamInfo().durationUs;

    // Pass keyframe index built during source_.open()
    controller_->init(&source_, &decoder_, &source_.keyframeIndex());

    state_ = PlaybackState::LOADED;
    return true;
}

void FFmpegBackend::play()
{
    controller_->play();
    state_ = PlaybackState::PLAYING;
}

void FFmpegBackend::pause()
{
    controller_->pause();
    state_ = PlaybackState::PAUSED;
}

void FFmpegBackend::stop()
{
    controller_->stop();
    decoder_.reset();
    state_ = PlaybackState::STOPPED;
}

bool FFmpegBackend::seek(int64_t us)
{
    if (!source_.isOpen()) return false;
    controller_->seekTo(us);
    return true;
}

bool FFmpegBackend::stepForward()
{
    if (!source_.isOpen()) return false;
    return controller_->stepForward();
}

bool FFmpegBackend::stepBackward()
{
    if (!source_.isOpen()) return false;
    return controller_->stepBackward();
}

void FFmpegBackend::connectToSink(QVideoSink* sink)
{
    controller_->connectToSink(sink);
}

PlaybackState FFmpegBackend::getState() const
{
    return state_;
}

int64_t FFmpegBackend::getDurationUs() const
{
    return durationUs_;
}

int64_t FFmpegBackend::getPositionUs() const
{
    return controller_->positionUs();
}

} // namespace videoengine
