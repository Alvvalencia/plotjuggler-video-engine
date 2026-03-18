#include "packet_queue.h"

namespace videoengine {

PacketQueue::PacketQueue(std::size_t capacity)
    : capacity_(capacity)
{
}

bool PacketQueue::push(VideoPacket packet)
{
    std::unique_lock lock(mutex_);
    notFull_.wait(lock, [this] { return shutdown_ || queue_.size() < capacity_; });
    if (shutdown_) {
        return false;
    }
    queue_.push_back(std::move(packet));
    notEmpty_.notify_one();
    return true;
}

std::optional<VideoPacket> PacketQueue::pop()
{
    std::unique_lock lock(mutex_);
    notEmpty_.wait(lock, [this] { return shutdown_ || !queue_.empty(); });
    if (shutdown_ && queue_.empty()) {
        return std::nullopt;
    }
    VideoPacket pkt = std::move(queue_.front());
    queue_.pop_front();
    notFull_.notify_one();
    return pkt;
}

void PacketQueue::clear()
{
    std::lock_guard lock(mutex_);
    queue_.clear();
    notFull_.notify_all();
}

void PacketQueue::reset()
{
    std::lock_guard lock(mutex_);
    queue_.clear();
    shutdown_ = false;
    notFull_.notify_all();
}

void PacketQueue::shutdown()
{
    std::lock_guard lock(mutex_);
    shutdown_ = true;
    notFull_.notify_all();
    notEmpty_.notify_all();
}

std::size_t PacketQueue::size() const
{
    std::lock_guard lock(mutex_);
    return queue_.size();
}

bool PacketQueue::empty() const
{
    std::lock_guard lock(mutex_);
    return queue_.empty();
}

} // namespace videoengine
