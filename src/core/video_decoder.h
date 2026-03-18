#ifndef VIDEOENGINE_CORE_VIDEO_DECODER_H
#define VIDEOENGINE_CORE_VIDEO_DECODER_H

#include "decoded_frame.h"
#include <optional>

struct AVCodecContext;
struct AVCodecParameters;
struct AVPacket;

namespace videoengine {

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Open decoder from codec parameters (from FileVideoSource::codecParameters())
    bool open(const AVCodecParameters* params);

    // Send packet and receive decoded frame.
    // Returns nullopt if decoder needs more data (EAGAIN) or on error.
    std::optional<DecodedFrame> decode(const AVPacket* packet);

    // Flush decoder — call after EOF to drain buffered frames
    std::optional<DecodedFrame> flush();

    // Reset decoder state (e.g. after seek)
    void reset();

    void close();
    bool isOpen() const;

private:
    AVCodecContext* codecCtx_ = nullptr;
};

} // namespace videoengine

#endif // VIDEOENGINE_CORE_VIDEO_DECODER_H
