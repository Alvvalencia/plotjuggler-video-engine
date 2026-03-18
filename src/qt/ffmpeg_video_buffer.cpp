#include "ffmpeg_video_buffer.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace videoengine {

FFmpegVideoBuffer::FFmpegVideoBuffer(DecodedFrame frame)
    : frame_(std::move(frame))
{
}

QAbstractVideoBuffer::MapData FFmpegVideoBuffer::map(QVideoFrame::MapMode mode)
{
    Q_UNUSED(mode);
    MapData data;

    if (mapped_ || !frame_.isValid()) {
        return data;
    }
    mapped_ = true;

    const AVFrame* f = frame_.raw();
    const int fmt = f->format;

    // Determine plane count from pixel format
    switch (fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
        data.planeCount = 3;
        break;
    case AV_PIX_FMT_NV12:
        data.planeCount = 2;
        break;
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_RGB24:
        data.planeCount = 1;
        break;
    default:
        data.planeCount = 0;
        return data;
    }

    for (int i = 0; i < data.planeCount; ++i) {
        data.data[i] = f->data[i];
        data.bytesPerLine[i] = f->linesize[i];

        // Calculate data size per plane
        int planeHeight = f->height;
        if (fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_NV12) {
            if (i > 0) planeHeight = (f->height + 1) / 2;
        } else if (fmt == AV_PIX_FMT_YUV422P) {
            // All planes same height for 4:2:2
        }
        data.dataSize[i] = f->linesize[i] * planeHeight;
    }

    return data;
}

void FFmpegVideoBuffer::unmap()
{
    mapped_ = false;
}

QVideoFrameFormat FFmpegVideoBuffer::format() const
{
    auto pixFmt = toQtPixelFormat(frame_.pixelFormat());
    return QVideoFrameFormat(QSize(frame_.width(), frame_.height()), pixFmt);
}

QVideoFrameFormat::PixelFormat FFmpegVideoBuffer::toQtPixelFormat(int avPixelFormat)
{
    switch (avPixelFormat) {
    case AV_PIX_FMT_YUV420P:  return QVideoFrameFormat::Format_YUV420P;
    case AV_PIX_FMT_NV12:     return QVideoFrameFormat::Format_NV12;
    case AV_PIX_FMT_RGBA:     return QVideoFrameFormat::Format_RGBA8888;
    case AV_PIX_FMT_BGRA:     return QVideoFrameFormat::Format_BGRA8888;
    case AV_PIX_FMT_YUV422P:  return QVideoFrameFormat::Format_YUV422P;
    default:                   return QVideoFrameFormat::Format_Invalid;
    }
}

} // namespace videoengine
