#include "MockVideoBackend.h"

namespace videoengine::testing {

bool MockVideoBackend::open(const QString& /*path*/)
{
    if (!openResult_) {
        return false;
    }
    position_ = 0;
    state_ = PlaybackState::LOADED;
    return true;
}

void MockVideoBackend::play()
{
    state_ = PlaybackState::PLAYING;
}

void MockVideoBackend::pause()
{
    state_ = PlaybackState::PAUSED;
}

void MockVideoBackend::stop()
{
    position_ = 0;
    state_ = PlaybackState::STOPPED;
}

bool MockVideoBackend::seek(int64_t us)
{
    int64_t maxPos = duration_ - frameDuration_;
    position_ = std::clamp(us, int64_t{0}, maxPos);
    return true;
}

bool MockVideoBackend::stepForward()
{
    if (position_ + frameDuration_ <= duration_ - frameDuration_) {
        position_ += frameDuration_;
        return true;
    }
    return false;
}

bool MockVideoBackend::stepBackward()
{
    if (position_ >= frameDuration_) {
        position_ -= frameDuration_;
        return true;
    }
    return false;
}

void MockVideoBackend::connectToSink(QVideoSink* sink)
{
    sink_ = sink;
}

PlaybackState MockVideoBackend::getState() const
{
    return state_;
}

int64_t MockVideoBackend::getDurationUs() const
{
    return duration_;
}

int64_t MockVideoBackend::getPositionUs() const
{
    return position_;
}

void MockVideoBackend::setOpenResult(bool result)
{
    openResult_ = result;
}

void MockVideoBackend::setDuration(int64_t us)
{
    duration_ = us;
}

void MockVideoBackend::setFrameDuration(int64_t us)
{
    frameDuration_ = us;
}

} // namespace videoengine::testing
