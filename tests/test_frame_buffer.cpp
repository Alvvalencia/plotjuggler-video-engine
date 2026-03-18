#include <gtest/gtest.h>
#include "../src/core/frame_buffer.h"
#include "../src/sources/file_video_source.h"
#include "../src/core/video_decoder.h"
#include <filesystem>
#include <thread>

using namespace videoengine;

namespace {

std::string testVideoPath()
{
    return (std::filesystem::path(__FILE__).parent_path()
            / "data" / "test_coarse.mp4").string();
}

// Decode N frames from the test video into a vector of {frame, ptsUs}.
std::vector<TimedFrame> decodeFrames(int count)
{
    FileVideoSource source;
    source.open(testVideoPath());
    VideoDecoder decoder;
    decoder.open(source.codecParameters());

    AVRational tb = source.streamInfo().timeBase;
    std::vector<TimedFrame> frames;
    for (int i = 0; i < count * 2 && static_cast<int>(frames.size()) < count; ++i) {
        auto pkt = source.readPacket();
        if (!pkt) break;
        auto f = decoder.decode(pkt->raw());
        if (f && f->isValid()) {
            int64_t ptsUs = av_rescale_q(f->pts(), tb, {1, 1'000'000});
            frames.push_back({std::move(*f), ptsUs});
        }
    }
    return frames;
}

} // namespace

// FB-01: Push and retrieve by PTS
TEST(FrameBufferTest, PushAndRetrieve)
{
    auto frames = decodeFrames(5);
    ASSERT_GE(frames.size(), 5u);

    FrameBuffer buf(64 * 1024 * 1024);
    for (auto& tf : frames) {
        buf.push(std::move(tf.frame), tf.ptsUs);
    }
    EXPECT_EQ(buf.frameCount(), 5u);

    // frameBefore the last frame's PTS should return the last frame
    auto result = buf.frameBefore(frames[4].ptsUs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ptsUs, frames[4].ptsUs);
}

// FB-02: frameAfter returns next frame
TEST(FrameBufferTest, FrameAfter)
{
    auto frames = decodeFrames(5);
    ASSERT_GE(frames.size(), 5u);

    FrameBuffer buf(64 * 1024 * 1024);
    for (auto& tf : frames) {
        buf.push(DecodedFrame(tf.frame), tf.ptsUs);
    }

    // frameAfter frame[1] should return frame[2]
    auto result = buf.frameAfter(frames[1].ptsUs);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ptsUs, frames[2].ptsUs);
}

// FB-03: frameBefore returns nullopt when all frames are after target
TEST(FrameBufferTest, FrameBeforeNone)
{
    auto frames = decodeFrames(3);
    ASSERT_GE(frames.size(), 3u);

    FrameBuffer buf(64 * 1024 * 1024);
    for (auto& tf : frames) {
        buf.push(DecodedFrame(tf.frame), tf.ptsUs);
    }

    // All frames have PTS > -1
    auto result = buf.frameBefore(-1);
    EXPECT_FALSE(result.has_value());
}

// FB-04: Eviction when over budget
TEST(FrameBufferTest, EvictionOnBudget)
{
    auto frames = decodeFrames(10);
    ASSERT_GE(frames.size(), 10u);

    // Tiny budget: only fits ~1 frame (1080p YUV420P ≈ 3.1 MB)
    FrameBuffer buf(4 * 1024 * 1024);
    for (auto& tf : frames) {
        buf.push(DecodedFrame(tf.frame), tf.ptsUs);
    }

    // Should have evicted most frames — only a few fit
    EXPECT_LT(buf.frameCount(), 5u);
    EXPECT_GT(buf.frameCount(), 0u);
}

// FB-05: clear empties buffer
TEST(FrameBufferTest, Clear)
{
    auto frames = decodeFrames(3);
    ASSERT_GE(frames.size(), 3u);

    FrameBuffer buf(64 * 1024 * 1024);
    for (auto& tf : frames) {
        buf.push(std::move(tf.frame), tf.ptsUs);
    }
    EXPECT_FALSE(buf.empty());

    buf.clear();
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.frameCount(), 0u);
}

// FB-06: Concurrent push and read
TEST(FrameBufferTest, ConcurrentAccess)
{
    auto frames = decodeFrames(30);
    ASSERT_GE(frames.size(), 20u);

    FrameBuffer buf(128 * 1024 * 1024);
    std::atomic<bool> done{false};

    // Writer thread
    std::thread writer([&] {
        for (auto& tf : frames) {
            buf.push(DecodedFrame(tf.frame), tf.ptsUs);
        }
        done = true;
    });

    // Reader thread — keep querying until writer is done
    int reads = 0;
    std::thread reader([&] {
        while (!done || buf.frameCount() > 0) {
            buf.frameBefore(15'000'000);
            buf.frameAfter(0);
            ++reads;
            if (reads > 10000) break; // safety
        }
    });

    writer.join();
    reader.join();

    // No crash, no deadlock is the pass criterion
    EXPECT_GT(buf.frameCount(), 0u);
}
