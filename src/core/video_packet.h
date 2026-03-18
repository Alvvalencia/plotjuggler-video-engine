#ifndef VIDEOENGINE_CORE_VIDEO_PACKET_H
#define VIDEOENGINE_CORE_VIDEO_PACKET_H

#include "types.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/packet.h>
}

namespace videoengine {

// Move-only RAII wrapper around AVPacket.
// Owns the packet data via av_packet_alloc / av_packet_free.
class VideoPacket {
public:
    VideoPacket() : packet_(av_packet_alloc()) {}
    ~VideoPacket() { av_packet_free(&packet_); }

    VideoPacket(const VideoPacket&) = delete;
    VideoPacket& operator=(const VideoPacket&) = delete;

    VideoPacket(VideoPacket&& other) noexcept
        : packet_(other.packet_)
    {
        other.packet_ = nullptr;
    }

    VideoPacket& operator=(VideoPacket&& other) noexcept
    {
        if (this != &other) {
            av_packet_free(&packet_);
            packet_ = other.packet_;
            other.packet_ = nullptr;
        }
        return *this;
    }

    int64_t pts() const { return packet_ ? packet_->pts : AV_NOPTS_VALUE; }
    int streamIndex() const { return packet_ ? packet_->stream_index : -1; }

    bool isKeyframe() const
    {
        return packet_ && (packet_->flags & AV_PKT_FLAG_KEY);
    }

    AVPacket* raw() { return packet_; }
    const AVPacket* raw() const { return packet_; }
    bool isValid() const { return packet_ && packet_->data; }

private:
    AVPacket* packet_;
};

} // namespace videoengine

#endif // VIDEOENGINE_CORE_VIDEO_PACKET_H
