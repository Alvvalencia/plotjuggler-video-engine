#ifndef VIDEOENGINE_FFMPEGBACKEND_H
#define VIDEOENGINE_FFMPEGBACKEND_H

#include "VideoEngine/IVideoBackend.h"
#include "core/playback_controller.h"
#include "sources/file_video_source.h"
#include "core/video_decoder.h"
#include <memory>

namespace videoengine {

// Production IVideoBackend implementation using FFmpeg.
// Owns the full pipeline: FileVideoSource → VideoDecoder → PlaybackController.
class FFmpegBackend : public QObject, public IVideoBackend {
    Q_OBJECT
public:
    explicit FFmpegBackend(QObject* parent = nullptr);
    ~FFmpegBackend() override;

    // IVideoBackend
    bool open(const QString& path) override;
    void play() override;
    void pause() override;
    void stop() override;
    bool seek(int64_t us) override;
    bool stepForward() override;
    bool stepBackward() override;
    void connectToSink(QVideoSink* sink) override;

    PlaybackState getState() const override;
    int64_t getDurationUs() const override;
    int64_t getPositionUs() const override;

private:
    FileVideoSource source_;
    VideoDecoder decoder_;
    std::unique_ptr<PlaybackController> controller_;
    PlaybackState state_ = PlaybackState::IDLE;
    int64_t durationUs_ = 0;
};

} // namespace videoengine

#endif // VIDEOENGINE_FFMPEGBACKEND_H
