#ifndef VIDEOENGINE_CORE_KEYFRAME_INDEX_H
#define VIDEOENGINE_CORE_KEYFRAME_INDEX_H

#include "types.h"
#include <optional>
#include <vector>
#include <string>

struct AVFormatContext;

namespace videoengine {

// Sorted list of keyframe timestamps for a video stream.
// Built by scanning packets at file open (O(n) in packet count,
// fast because we only read headers — no decoding).
// Used by PlaybackController for frame-accurate seeking.
class KeyframeIndex {
public:
    struct Entry {
        Timestamp ptsUs;  // microseconds (canonical time unit)
        int64_t rawPts;   // PTS in stream time_base units (for av_seek_frame)
    };

    // Scan all packets in the stream and record keyframe positions.
    // Seeks the source back to the beginning after scanning.
    void build(AVFormatContext* ctx, int streamIndex);

    // Nearest keyframe with PTS <= targetUs.
    // This is the seek target for frame-accurate seeking:
    // seek to this keyframe, then decode forward to the target.
    std::optional<Entry> nearestBefore(Timestamp targetUs) const;

    // Nearest keyframe with PTS >= targetUs.
    std::optional<Entry> nearestAfter(Timestamp targetUs) const;

    const std::vector<Entry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }
    std::size_t size() const { return entries_.size(); }

private:
    std::vector<Entry> entries_; // sorted ascending by ptsUs
};

} // namespace videoengine

#endif // VIDEOENGINE_CORE_KEYFRAME_INDEX_H
