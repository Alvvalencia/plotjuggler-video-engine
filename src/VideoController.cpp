#include "VideoEngine/VideoController.h"

namespace videoengine {

VideoController::VideoController(std::unique_ptr<IVideoBackend> backend,
                                 QObject* parent)
    : QObject(parent)
    , backend_(std::move(backend))
{
}

VideoController::~VideoController() = default;

bool VideoController::open(const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }

    if (!backend_->open(path)) {
        return false;
    }

    setState(PlaybackState::LOADED);
    return true;
}

bool VideoController::play()
{
    switch (state_) {
    case PlaybackState::LOADED:
    case PlaybackState::PAUSED:
    case PlaybackState::STOPPED:
        backend_->play();
        setState(PlaybackState::PLAYING);
        return true;
    case PlaybackState::PLAYING:
        return true; // no-op
    case PlaybackState::IDLE:
    case PlaybackState::ERROR:
        return false;
    }
    return false;
}

bool VideoController::pause()
{
    switch (state_) {
    case PlaybackState::PLAYING:
        backend_->pause();
        setState(PlaybackState::PAUSED);
        return true;
    case PlaybackState::PAUSED:
        return true; // no-op
    default:
        return false;
    }
}

bool VideoController::stop()
{
    switch (state_) {
    case PlaybackState::LOADED:
    case PlaybackState::PLAYING:
    case PlaybackState::PAUSED:
        backend_->stop();
        setState(PlaybackState::STOPPED);
        return true;
    case PlaybackState::STOPPED:
        return true; // no-op
    case PlaybackState::IDLE:
    case PlaybackState::ERROR:
        return false;
    }
    return false;
}

bool VideoController::seek(int64_t us)
{
    if (state_ == PlaybackState::IDLE || state_ == PlaybackState::ERROR) {
        return false;
    }

    if (us < 0) {
        return false;
    }

    backend_->seek(us);
    return true;
}

bool VideoController::stepForward()
{
    if (state_ != PlaybackState::PAUSED && state_ != PlaybackState::LOADED
        && state_ != PlaybackState::STOPPED) {
        return false;
    }
    if (!backend_->stepForward()) {
        return false;
    }
    // Stepping keeps us in PAUSED state
    setState(PlaybackState::PAUSED);
    return true;
}

bool VideoController::stepBackward()
{
    if (state_ != PlaybackState::PAUSED && state_ != PlaybackState::LOADED
        && state_ != PlaybackState::STOPPED) {
        return false;
    }
    if (!backend_->stepBackward()) {
        return false;
    }
    setState(PlaybackState::PAUSED);
    return true;
}

void VideoController::connectToSink(QVideoSink* sink)
{
    backend_->connectToSink(sink);
}

PlaybackState VideoController::getState() const
{
    return state_;
}

int64_t VideoController::getDurationUs() const
{
    return backend_->getDurationUs();
}

int64_t VideoController::getPositionUs() const
{
    return backend_->getPositionUs();
}

void VideoController::setState(PlaybackState newState)
{
    if (state_ != newState) {
        state_ = newState;
        emit stateChanged(state_);
    }
}

} // namespace videoengine
