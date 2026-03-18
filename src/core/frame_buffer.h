#ifndef VIDEOENGINE_CORE_FRAME_BUFFER_H
#define VIDEOENGINE_CORE_FRAME_BUFFER_H

#include "decoded_frame.h"
#include "types.h"
#include <deque>
#include <mutex>
#include <optional>

namespace videoengine {

// A frame paired with its PTS in microseconds.
struct TimedFrame {
    DecodedFrame frame;
    int64_t ptsUs;
};

// Thread-safe ring buffer of decoded frames indexed by PTS.
// Evicts oldest frames when the memory budget is exceeded.
// Used for:
//   - Pipeline buffer (decode thread → display timer)
//   - Frame cache (step backward without re-decoding)
class FrameBuffer {
public:
    explicit FrameBuffer(std::size_t budgetBytes = 256 * 1024 * 1024);

    // Push a frame with its PTS in µs. Evicts oldest if over budget.
    void push(DecodedFrame frame, int64_t ptsUs);

    // Latest frame with PTS <= targetPtsUs. Returns nullopt if none.
    std::optional<TimedFrame> frameBefore(int64_t ptsUs) const;

    // Earliest frame with PTS > currentPtsUs. Returns nullopt if none.
    std::optional<TimedFrame> frameAfter(int64_t ptsUs) const;

    void clear();
    bool empty() const;
    std::size_t frameCount() const;
    std::size_t usedBytes() const;

private:
    static std::size_t estimateBytes(const DecodedFrame& f);

    struct Entry {
        int64_t ptsUs;
        DecodedFrame frame;
        std::size_t bytes;
    };

    mutable std::mutex mutex_;
    std::deque<Entry> entries_; // sorted by ptsUs (decode order ≈ display order)
    std::size_t budgetBytes_;
    std::size_t usedBytes_ = 0;
};

} // namespace videoengine

#endif // VIDEOENGINE_CORE_FRAME_BUFFER_H
