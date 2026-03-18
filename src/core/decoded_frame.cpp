#include "decoded_frame.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace videoengine {

DecodedFrame::DecodedFrame()
    : frame_(av_frame_alloc())
{
}

DecodedFrame::~DecodedFrame()
{
    if (frame_) {
        av_frame_unref(frame_);
        av_frame_free(&frame_);
    }
}

DecodedFrame::DecodedFrame(const DecodedFrame& other)
    : frame_(av_frame_alloc())
{
    if (other.frame_ && other.frame_->data[0]) {
        av_frame_ref(frame_, other.frame_);
    }
}

DecodedFrame& DecodedFrame::operator=(const DecodedFrame& other)
{
    if (this != &other) {
        av_frame_unref(frame_);
        if (other.frame_ && other.frame_->data[0]) {
            av_frame_ref(frame_, other.frame_);
        }
    }
    return *this;
}

DecodedFrame::DecodedFrame(DecodedFrame&& other) noexcept
    : frame_(other.frame_)
{
    other.frame_ = nullptr;
}

DecodedFrame& DecodedFrame::operator=(DecodedFrame&& other) noexcept
{
    if (this != &other) {
        if (frame_) {
            av_frame_unref(frame_);
            av_frame_free(&frame_);
        }
        frame_ = other.frame_;
        other.frame_ = nullptr;
    }
    return *this;
}

DecodedFrame DecodedFrame::fromAVFrame(const AVFrame* src)
{
    DecodedFrame f;
    if (src) {
        av_frame_ref(f.frame_, src);
    }
    return f;
}

int DecodedFrame::width() const { return frame_ ? frame_->width : 0; }
int DecodedFrame::height() const { return frame_ ? frame_->height : 0; }
int DecodedFrame::pixelFormat() const { return frame_ ? frame_->format : -1; }
int64_t DecodedFrame::pts() const { return frame_ ? frame_->pts : AV_NOPTS_VALUE; }

uint8_t* const* DecodedFrame::data() const { return frame_ ? frame_->data : nullptr; }
const int* DecodedFrame::linesize() const { return frame_ ? frame_->linesize : nullptr; }

AVFrame* DecodedFrame::raw() { return frame_; }
const AVFrame* DecodedFrame::raw() const { return frame_; }
bool DecodedFrame::isValid() const { return frame_ && frame_->data[0]; }

} // namespace videoengine
