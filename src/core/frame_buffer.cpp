#include "frame_buffer.h"

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace videoengine {

FrameBuffer::FrameBuffer(std::size_t budgetBytes)
    : budgetBytes_(budgetBytes)
{
}

void FrameBuffer::push(DecodedFrame frame, int64_t ptsUs)
{
    std::size_t bytes = estimateBytes(frame);

    std::lock_guard lock(mutex_);

    // Evict oldest frames until there's room
    while (usedBytes_ + bytes > budgetBytes_ && !entries_.empty()) {
        usedBytes_ -= entries_.front().bytes;
        entries_.pop_front();
    }

    entries_.push_back({ptsUs, std::move(frame), bytes});
    usedBytes_ += bytes;
}

std::optional<TimedFrame> FrameBuffer::frameBefore(int64_t ptsUs) const
{
    std::lock_guard lock(mutex_);

    const Entry* best = nullptr;
    for (const auto& e : entries_) {
        if (e.ptsUs <= ptsUs) {
            best = &e;
        } else {
            break; // entries are sorted ascending
        }
    }

    if (best) {
        return TimedFrame{DecodedFrame(best->frame), best->ptsUs};
    }
    return std::nullopt;
}

std::optional<TimedFrame> FrameBuffer::frameAfter(int64_t ptsUs) const
{
    std::lock_guard lock(mutex_);

    for (const auto& e : entries_) {
        if (e.ptsUs > ptsUs) {
            return TimedFrame{DecodedFrame(e.frame), e.ptsUs};
        }
    }
    return std::nullopt;
}

void FrameBuffer::clear()
{
    std::lock_guard lock(mutex_);
    entries_.clear();
    usedBytes_ = 0;
}

bool FrameBuffer::empty() const
{
    std::lock_guard lock(mutex_);
    return entries_.empty();
}

std::size_t FrameBuffer::frameCount() const
{
    std::lock_guard lock(mutex_);
    return entries_.size();
}

std::size_t FrameBuffer::usedBytes() const
{
    std::lock_guard lock(mutex_);
    return usedBytes_;
}

std::size_t FrameBuffer::estimateBytes(const DecodedFrame& f)
{
    if (!f.isValid()) return 0;
    int w = f.width();
    int h = f.height();
    // Estimate based on pixel format: YUV420P = 1.5 bytes/pixel,
    // YUV422P = 2, RGBA/BGRA = 4. Default to 3 (conservative).
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(
        static_cast<AVPixelFormat>(f.pixelFormat()));
    if (!desc) return static_cast<std::size_t>(w) * h * 3;

    int bitsPerPixel = av_get_bits_per_pixel(desc);
    return static_cast<std::size_t>(w) * h * bitsPerPixel / 8;
}

} // namespace videoengine
