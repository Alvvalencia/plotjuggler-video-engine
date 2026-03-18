#include <gtest/gtest.h>
#include "../src/core/packet_queue.h"
#include <thread>
#include <chrono>

using namespace videoengine;

// Helper: create a VideoPacket with a given pts value for identification
static VideoPacket makePacket(int64_t pts)
{
    VideoPacket pkt;
    pkt.raw()->pts = pts;
    pkt.raw()->data = reinterpret_cast<uint8_t*>(1); // non-null to make isValid() true
    pkt.raw()->size = 0;
    return pkt;
}

// PQ-01: Push + pop single packet
TEST(PacketQueueTest, PushPopSingle)
{
    PacketQueue q(4);
    ASSERT_TRUE(q.push(makePacket(42)));
    auto pkt = q.pop();
    ASSERT_TRUE(pkt.has_value());
    EXPECT_EQ(pkt->pts(), 42);
    EXPECT_TRUE(q.empty());
}

// PQ-02: Push to capacity, then pop all — FIFO order
TEST(PacketQueueTest, PushToCapacityFIFO)
{
    constexpr int cap = 8;
    PacketQueue q(cap);

    for (int i = 0; i < cap; ++i) {
        ASSERT_TRUE(q.push(makePacket(i)));
    }
    EXPECT_EQ(q.size(), static_cast<std::size_t>(cap));

    for (int i = 0; i < cap; ++i) {
        auto pkt = q.pop();
        ASSERT_TRUE(pkt.has_value());
        EXPECT_EQ(pkt->pts(), i);
    }
    EXPECT_TRUE(q.empty());
}

// PQ-03: clear() empties queue
TEST(PacketQueueTest, ClearEmpties)
{
    PacketQueue q(8);
    q.push(makePacket(1));
    q.push(makePacket(2));
    q.push(makePacket(3));
    EXPECT_EQ(q.size(), 3u);

    q.clear();
    EXPECT_TRUE(q.empty());
}

// PQ-04: shutdown() unblocks waiting pop()
TEST(PacketQueueTest, ShutdownUnblocksPop)
{
    PacketQueue q(4);

    std::optional<VideoPacket> result;
    std::thread consumer([&] {
        result = q.pop(); // will block — queue is empty
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.shutdown();
    consumer.join();

    EXPECT_FALSE(result.has_value()); // nullopt on shutdown
}

// PQ-05: shutdown() unblocks blocked push()
TEST(PacketQueueTest, ShutdownUnblocksPush)
{
    PacketQueue q(1);
    q.push(makePacket(0)); // fill to capacity

    bool pushResult = true;
    std::thread producer([&] {
        pushResult = q.push(makePacket(1)); // will block — queue full
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.shutdown();
    producer.join();

    EXPECT_FALSE(pushResult); // false on shutdown
}

// PQ-06: 1000 push/pop cycles across 2 threads — no deadlock
TEST(PacketQueueTest, ConcurrentPushPop)
{
    constexpr int count = 1000;
    PacketQueue q(16);

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            if (!q.push(makePacket(i))) break;
        }
        q.shutdown();
    });

    int received = 0;
    int64_t lastPts = -1;
    std::thread consumer([&] {
        while (auto pkt = q.pop()) {
            EXPECT_GT(pkt->pts(), lastPts); // FIFO order
            lastPts = pkt->pts();
            ++received;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received, count);
}
