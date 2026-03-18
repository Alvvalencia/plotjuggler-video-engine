#ifndef VIDEOENGINE_CORE_PACKET_QUEUE_H
#define VIDEOENGINE_CORE_PACKET_QUEUE_H

#include "video_packet.h"
#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace videoengine {

// Bounded thread-safe queue for passing VideoPackets between threads.
// SPSC pattern (single producer, single consumer) but safe for MPMC.
// push() blocks when full, pop() blocks when empty, shutdown() unblocks all.
class PacketQueue {
public:
    explicit PacketQueue(std::size_t capacity = 64);

    // Push packet. Blocks if full. Returns false if shutdown.
    bool push(VideoPacket packet);

    // Pop packet. Blocks if empty. Returns nullopt if shutdown.
    std::optional<VideoPacket> pop();

    // Remove all queued packets.
    void clear();

    // Unblock all waiting push/pop calls. Queue becomes permanently shut down.
    void shutdown();

    // Clear queue and reset shutdown flag — ready for reuse.
    void reset();

    std::size_t size() const;
    bool empty() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::deque<VideoPacket> queue_;
    std::size_t capacity_;
    bool shutdown_ = false;
};

} // namespace videoengine

#endif // VIDEOENGINE_CORE_PACKET_QUEUE_H
