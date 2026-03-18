#include <gtest/gtest.h>
#include "VideoEngine/VideoController.h"
#include "MockVideoBackend.h"

using namespace videoengine;
using namespace videoengine::testing;

class SeekTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto mock = std::make_unique<MockVideoBackend>();
        mock_ = mock.get();
        ctrl_ = std::make_unique<VideoController>(std::move(mock));
        ctrl_->open("test.mp4");
    }

    MockVideoBackend* mock_ = nullptr;
    std::unique_ptr<VideoController> ctrl_;
};

// UT-SK-01: seek(0) → position 0
TEST_F(SeekTest, SeekToZero)
{
    EXPECT_TRUE(ctrl_->seek(0));
    EXPECT_EQ(ctrl_->getPositionUs(), 0);
}

// UT-SK-02: seek(5M) with 10M duration → position 5M
TEST_F(SeekTest, SeekToMiddle)
{
    EXPECT_TRUE(ctrl_->seek(5'000'000));
    EXPECT_EQ(ctrl_->getPositionUs(), 5'000'000);
}

// UT-SK-03: seek(-1) → false, position unchanged
TEST_F(SeekTest, SeekNegative)
{
    ctrl_->seek(5'000'000);
    int64_t posBefore = ctrl_->getPositionUs();
    EXPECT_FALSE(ctrl_->seek(-1));
    EXPECT_EQ(ctrl_->getPositionUs(), posBefore);
}

// UT-SK-04: seek(99M) → clamped by mock to duration - frameDuration
TEST_F(SeekTest, SeekBeyondDuration)
{
    EXPECT_TRUE(ctrl_->seek(99'000'000));
    // Mock clamps to duration(10M) - frameDuration(33333) = 9966667
    EXPECT_EQ(ctrl_->getPositionUs(), 10'000'000 - 33'333);
}

// UT-SK-05: seek during PLAYING → stays PLAYING
TEST_F(SeekTest, SeekDuringPlaying)
{
    ctrl_->play();
    EXPECT_TRUE(ctrl_->seek(3'000'000));
    EXPECT_EQ(ctrl_->getState(), PlaybackState::PLAYING);
    EXPECT_EQ(ctrl_->getPositionUs(), 3'000'000);
}

// UT-SK-06: seek during PAUSED → stays PAUSED
TEST_F(SeekTest, SeekDuringPaused)
{
    ctrl_->play();
    ctrl_->pause();
    EXPECT_TRUE(ctrl_->seek(7'000'000));
    EXPECT_EQ(ctrl_->getState(), PlaybackState::PAUSED);
    EXPECT_EQ(ctrl_->getPositionUs(), 7'000'000);
}
