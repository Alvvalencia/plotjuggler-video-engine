#ifndef VIDEOENGINE_IVIDEOBACKEND_H
#define VIDEOENGINE_IVIDEOBACKEND_H

#include <cstdint>
#include <QString>
#include "VideoEngine/PlaybackState.h"

class QVideoSink;

namespace videoengine {

class IVideoBackend {
public:
    virtual ~IVideoBackend() = default;

    virtual bool open(const QString& path) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual bool seek(int64_t us) = 0;
    virtual bool stepForward() = 0;
    virtual bool stepBackward() = 0;
    virtual void connectToSink(QVideoSink* sink) = 0;

    virtual PlaybackState getState() const = 0;
    virtual int64_t getDurationUs() const = 0;
    virtual int64_t getPositionUs() const = 0;
};

} // namespace videoengine

#endif // VIDEOENGINE_IVIDEOBACKEND_H
