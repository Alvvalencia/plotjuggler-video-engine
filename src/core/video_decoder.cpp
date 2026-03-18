#include "video_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace videoengine {

VideoDecoder::~VideoDecoder()
{
    close();
}

bool VideoDecoder::open(const AVCodecParameters* params)
{
    close();

    if (!params) {
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, params) < 0) {
        close();
        return false;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        close();
        return false;
    }

    return true;
}

std::optional<DecodedFrame> VideoDecoder::decode(const AVPacket* packet)
{
    if (!codecCtx_) {
        return std::nullopt;
    }

    int ret = avcodec_send_packet(codecCtx_, packet);
    if (ret < 0) {
        return std::nullopt;
    }

    AVFrame* tmp = av_frame_alloc();
    ret = avcodec_receive_frame(codecCtx_, tmp);
    if (ret < 0) {
        av_frame_free(&tmp);
        return std::nullopt; // EAGAIN or error
    }

    DecodedFrame frame = DecodedFrame::fromAVFrame(tmp);
    av_frame_free(&tmp);
    return frame;
}

std::optional<DecodedFrame> VideoDecoder::flush()
{
    if (!codecCtx_) {
        return std::nullopt;
    }

    // Send nullptr to signal EOF
    avcodec_send_packet(codecCtx_, nullptr);

    AVFrame* tmp = av_frame_alloc();
    int ret = avcodec_receive_frame(codecCtx_, tmp);
    if (ret < 0) {
        av_frame_free(&tmp);
        return std::nullopt;
    }

    DecodedFrame frame = DecodedFrame::fromAVFrame(tmp);
    av_frame_free(&tmp);
    return frame;
}

void VideoDecoder::reset()
{
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_);
    }
}

void VideoDecoder::close()
{
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
}

bool VideoDecoder::isOpen() const
{
    return codecCtx_ != nullptr;
}

} // namespace videoengine
