#ifndef VIDEOENGINE_SOURCES_FILE_VIDEO_SOURCE_H
#define VIDEOENGINE_SOURCES_FILE_VIDEO_SOURCE_H

#include "../core/video_source.h"
#include "../core/keyframe_index.h"

struct AVFormatContext;
struct AVCodecParameters;

namespace videoengine {

class FileVideoSource : public VideoSource {
public:
    FileVideoSource() = default;
    ~FileVideoSource() override;

    bool open(const std::string& path) override;
    std::optional<VideoPacket> readPacket() override;
    bool seekTo(Timestamp us) override;
    VideoStreamInfo streamInfo() const override;
    void close() override;
    bool isOpen() const override;

    const AVCodecParameters* codecParameters() const;
    const KeyframeIndex& keyframeIndex() const { return keyframeIndex_; }

    // Access the format context for KeyframeIndex building (called during open)
    AVFormatContext* formatContext() { return formatCtx_; }
    int videoStream() const { return videoStreamIndex_; }

private:
    AVFormatContext* formatCtx_ = nullptr;
    int videoStreamIndex_ = -1;
    VideoStreamInfo info_;
    KeyframeIndex keyframeIndex_;
};

} // namespace videoengine

#endif // VIDEOENGINE_SOURCES_FILE_VIDEO_SOURCE_H
