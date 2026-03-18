#ifndef VIDEOENGINE_VIDEOCONTROLLER_H
#define VIDEOENGINE_VIDEOCONTROLLER_H

#include <memory>
#include <cstdint>
#include <QObject>
#include <QString>
#include "VideoEngine/PlaybackState.h"
#include "VideoEngine/IVideoBackend.h"

class QVideoSink;

namespace videoengine {

class VideoController : public QObject {
    Q_OBJECT
public:
    explicit VideoController(std::unique_ptr<IVideoBackend> backend,
                             QObject* parent = nullptr);
    ~VideoController() override;

    bool open(const QString& path);
    bool play();
    bool pause();
    bool stop();
    bool seek(int64_t us);
    bool stepForward();
    bool stepBackward();

    void connectToSink(QVideoSink* sink);

    PlaybackState getState() const;
    int64_t getDurationUs() const;
    int64_t getPositionUs() const;

signals:
    void stateChanged(videoengine::PlaybackState newState);
    void positionChanged(int64_t us);
    void errorOccurred(const QString& message);

private:
    void setState(PlaybackState newState);

    std::unique_ptr<IVideoBackend> backend_;
    PlaybackState state_ = PlaybackState::IDLE;
};

} // namespace videoengine

#endif // VIDEOENGINE_VIDEOCONTROLLER_H
