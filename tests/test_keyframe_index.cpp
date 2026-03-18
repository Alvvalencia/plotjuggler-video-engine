#include <gtest/gtest.h>
#include "../src/sources/file_video_source.h"
#include "../src/core/keyframe_index.h"
#include <filesystem>

using namespace videoengine;

namespace {

std::string testVideoPath()
{
    return (std::filesystem::path(__FILE__).parent_path()
            / "data" / "test_coarse.mp4").string();
}

} // namespace

// KF-01: Build index from test video — non-empty, has expected keyframes
TEST(KeyframeIndexTest, BuildFromTestVideo)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));

    const auto& index = source.keyframeIndex();
    EXPECT_FALSE(index.empty());

    // test_coarse.mp4: 30s at 30fps, GOP=30 → one keyframe per second → ~30 keyframes
    EXPECT_GE(index.size(), 25u);
    EXPECT_LE(index.size(), 35u);

    // First keyframe should be at or near PTS 0
    EXPECT_NEAR(index.entries().front().ptsUs, 0, 50'000);
}

// KF-02: nearestBefore at midpoint returns correct keyframe
TEST(KeyframeIndexTest, NearestBeforeMidpoint)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));
    const auto& index = source.keyframeIndex();

    // Seek to 15.5 seconds — nearest keyframe before should be at ~15s
    auto kf = index.nearestBefore(15'500'000);
    ASSERT_TRUE(kf.has_value());
    EXPECT_LE(kf->ptsUs, 15'500'000);
    EXPECT_GE(kf->ptsUs, 14'500'000); // within 1 second before
}

// KF-03: nearestBefore(0) returns first keyframe
TEST(KeyframeIndexTest, NearestBeforeZero)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));
    const auto& index = source.keyframeIndex();

    auto kf = index.nearestBefore(0);
    ASSERT_TRUE(kf.has_value());
    EXPECT_NEAR(kf->ptsUs, 0, 50'000);
}

// KF-04: nearestBefore past end returns last keyframe
TEST(KeyframeIndexTest, NearestBeforePastEnd)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));
    const auto& index = source.keyframeIndex();

    auto kf = index.nearestBefore(999'000'000);
    ASSERT_TRUE(kf.has_value());
    // Last keyframe should be around second 29
    EXPECT_GE(kf->ptsUs, 28'000'000);
}

// KF-05: nearestAfter returns correct keyframe
TEST(KeyframeIndexTest, NearestAfterMidpoint)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));
    const auto& index = source.keyframeIndex();

    auto kf = index.nearestAfter(10'500'000);
    ASSERT_TRUE(kf.has_value());
    EXPECT_GE(kf->ptsUs, 10'500'000);
    EXPECT_LE(kf->ptsUs, 11'500'000); // within 1 second after
}

// KF-06: Empty index returns nullopt
TEST(KeyframeIndexTest, EmptyIndex)
{
    KeyframeIndex index;
    EXPECT_TRUE(index.empty());
    EXPECT_FALSE(index.nearestBefore(1'000'000).has_value());
    EXPECT_FALSE(index.nearestAfter(1'000'000).has_value());
}
