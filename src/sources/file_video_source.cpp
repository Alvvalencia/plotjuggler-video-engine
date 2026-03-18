#include "file_video_source.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/codec_par.h>
}

namespace videoengine {

namespace {

Codec codecFromId(AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_H264:  return Codec::H264;
    case AV_CODEC_ID_H265:  return Codec::H265;
    case AV_CODEC_ID_AV1:   return Codec::AV1;
    default:                 return Codec::Unknown;
    }
}

} // namespace

FileVideoSource::~FileVideoSource()
{
    close();
}

bool FileVideoSource::open(const std::string& path)
{
    close();

    if (avformat_open_input(&formatCtx_, path.c_str(), nullptr, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(formatCtx_, nullptr) < 0) {
        close();
        return false;
    }

    videoStreamIndex_ = av_find_best_stream(
        formatCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

    if (videoStreamIndex_ < 0) {
        close();
        return false;
    }

    const AVStream* stream = formatCtx_->streams[videoStreamIndex_];
    const AVCodecParameters* params = stream->codecpar;

    info_.codec = codecFromId(params->codec_id);
    info_.width = params->width;
    info_.height = params->height;
    info_.pixelFormat = params->format;
    info_.timeBase = stream->time_base;

    if (stream->avg_frame_rate.den > 0) {
        info_.fps = av_q2d(stream->avg_frame_rate);
    } else if (stream->r_frame_rate.den > 0) {
        info_.fps = av_q2d(stream->r_frame_rate);
    }

    // Duration in microseconds
    if (stream->duration != AV_NOPTS_VALUE) {
        info_.durationUs = av_rescale_q(
            stream->duration, stream->time_base, {1, 1'000'000});
    } else if (formatCtx_->duration != AV_NOPTS_VALUE) {
        info_.durationUs = formatCtx_->duration; // already in AV_TIME_BASE (µs)
    }

    // Build keyframe index by scanning packet headers (no decoding)
    keyframeIndex_.build(formatCtx_, videoStreamIndex_);

    return true;
}

std::optional<VideoPacket> FileVideoSource::readPacket()
{
    if (!formatCtx_) {
        return std::nullopt;
    }

    VideoPacket pkt;
    while (true) {
        int ret = av_read_frame(formatCtx_, pkt.raw());
        if (ret < 0) {
            return std::nullopt; // EOF or error
        }
        if (pkt.streamIndex() == videoStreamIndex_) {
            return pkt;
        }
        // Not our stream — unref and try next packet
        av_packet_unref(pkt.raw());
    }
}

bool FileVideoSource::seekTo(Timestamp us)
{
    if (!formatCtx_) {
        return false;
    }

    int64_t seekTarget = av_rescale_q(
        us, {1, 1'000'000}, formatCtx_->streams[videoStreamIndex_]->time_base);

    return av_seek_frame(
        formatCtx_, videoStreamIndex_, seekTarget,
        AVSEEK_FLAG_BACKWARD) >= 0;
}

VideoStreamInfo FileVideoSource::streamInfo() const
{
    return info_;
}

void FileVideoSource::close()
{
    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
    }
    videoStreamIndex_ = -1;
    info_ = {};
}

bool FileVideoSource::isOpen() const
{
    return formatCtx_ != nullptr;
}

const AVCodecParameters* FileVideoSource::codecParameters() const
{
    if (!formatCtx_ || videoStreamIndex_ < 0) {
        return nullptr;
    }
    return formatCtx_->streams[videoStreamIndex_]->codecpar;
}

} // namespace videoengine
