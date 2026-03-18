#include "keyframe_index.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <algorithm>

namespace videoengine {

void KeyframeIndex::build(AVFormatContext* ctx, int streamIndex)
{
    entries_.clear();
    if (!ctx || streamIndex < 0) return;

    const AVStream* stream = ctx->streams[streamIndex];
    const AVRational timeBase = stream->time_base;

    // Save current position and scan from the start
    av_seek_frame(ctx, streamIndex, 0, AVSEEK_FLAG_BACKWARD);

    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(ctx, pkt) >= 0) {
        if (pkt->stream_index == streamIndex &&
            (pkt->flags & AV_PKT_FLAG_KEY)) {
            int64_t ptsUs = av_rescale_q(pkt->pts, timeBase, {1, 1'000'000});
            entries_.push_back({ptsUs, pkt->pts});
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    // Sort by PTS (should already be sorted for well-formed files)
    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.ptsUs < b.ptsUs; });

    // Seek back to start so the source is ready for playback
    av_seek_frame(ctx, streamIndex, 0, AVSEEK_FLAG_BACKWARD);
}

std::optional<KeyframeIndex::Entry>
KeyframeIndex::nearestBefore(Timestamp targetUs) const
{
    if (entries_.empty()) return std::nullopt;

    // Binary search: find last entry with ptsUs <= targetUs
    auto it = std::upper_bound(entries_.begin(), entries_.end(), targetUs,
        [](Timestamp val, const Entry& e) { return val < e.ptsUs; });

    if (it == entries_.begin()) {
        // All keyframes are after targetUs — return first as fallback
        return entries_.front();
    }
    return *std::prev(it);
}

std::optional<KeyframeIndex::Entry>
KeyframeIndex::nearestAfter(Timestamp targetUs) const
{
    if (entries_.empty()) return std::nullopt;

    // Binary search: find first entry with ptsUs >= targetUs
    auto it = std::lower_bound(entries_.begin(), entries_.end(), targetUs,
        [](const Entry& e, Timestamp val) { return e.ptsUs < val; });

    if (it == entries_.end()) {
        return entries_.back(); // fallback to last keyframe
    }
    return *it;
}

} // namespace videoengine
