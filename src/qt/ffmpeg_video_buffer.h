#ifndef VIDEOENGINE_QT_FFMPEG_VIDEO_BUFFER_H
#define VIDEOENGINE_QT_FFMPEG_VIDEO_BUFFER_H

#include "../core/decoded_frame.h"
#include <QAbstractVideoBuffer>
#include <QVideoFrameFormat>

namespace videoengine {

// Bridges DecodedFrame (AVFrame) to Qt's video rendering pipeline.
// Qt takes ownership via unique_ptr<QAbstractVideoBuffer>.
// When Qt destroys the QVideoFrame → destroys this buffer → DecodedFrame
// destructor calls av_frame_unref.
class FFmpegVideoBuffer : public QAbstractVideoBuffer {
public:
    explicit FFmpegVideoBuffer(DecodedFrame frame);

    MapData map(QVideoFrame::MapMode mode) override;
    void unmap() override;
    QVideoFrameFormat format() const override;

    // Convert AVPixelFormat to Qt pixel format. Returns Format_Invalid for unsupported.
    static QVideoFrameFormat::PixelFormat toQtPixelFormat(int avPixelFormat);

private:
    DecodedFrame frame_;
    bool mapped_ = false;
};

} // namespace videoengine

#endif // VIDEOENGINE_QT_FFMPEG_VIDEO_BUFFER_H
