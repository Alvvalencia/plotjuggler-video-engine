#ifndef VIDEOENGINE_CORE_VIDEO_SOURCE_H
#define VIDEOENGINE_CORE_VIDEO_SOURCE_H

#include "types.h"
#include "video_packet.h"
#include <optional>
#include <string>

extern "C" {
#include <libavutil/rational.h>
}

namespace videoengine {

struct VideoStreamInfo {
    Codec codec = Codec::Unknown;
    int width = 0;
    int height = 0;
    int pixelFormat = -1;  // AVPixelFormat
    double fps = 0.0;
    AVRational timeBase = {0, 1};
    Duration durationUs = 0;
};

// Abstract interface for reading video packets from a source.
class VideoSource {
public:
    virtual ~VideoSource() = default;

    virtual bool open(const std::string& path) = 0;
    virtual std::optional<VideoPacket> readPacket() = 0;
    virtual bool seekTo(Timestamp us) = 0;
    virtual VideoStreamInfo streamInfo() const = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
};

} // namespace videoengine

#endif // VIDEOENGINE_CORE_VIDEO_SOURCE_H
