#include <gtest/gtest.h>
#include "VideoEngine/VideoController.h"
#include "MockVideoBackend.h"

using namespace videoengine;
using namespace videoengine::testing;

class RobustnessTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto mock = std::make_unique<MockVideoBackend>();
        mock_ = mock.get();
        ctrl_ = std::make_unique<VideoController>(std::move(mock));
    }

    MockVideoBackend* mock_ = nullptr;
    std::unique_ptr<VideoController> ctrl_;
};

// UT-RB-01: seek from IDLE → false
TEST_F(RobustnessTest, SeekFromIdle)
{
    EXPECT_FALSE(ctrl_->seek(1'000'000));
    EXPECT_EQ(ctrl_->getState(), PlaybackState::IDLE);
}

// UT-RB-02: play→play → no crash, PLAYING
TEST_F(RobustnessTest, DoublePlay)
{
    ctrl_->open("test.mp4");
    EXPECT_TRUE(ctrl_->play());
    EXPECT_TRUE(ctrl_->play());
    EXPECT_EQ(ctrl_->getState(), PlaybackState::PLAYING);
}

// UT-RB-03: destructor during PLAYING → no crash
TEST_F(RobustnessTest, DestructorDuringPlaying)
{
    ctrl_->open("test.mp4");
    ctrl_->play();
    EXPECT_EQ(ctrl_->getState(), PlaybackState::PLAYING);
    ctrl_.reset(); // destroy while playing — must not crash
    SUCCEED();
}

// UT-RB-04: rapid seek x3 → position == last seek
TEST_F(RobustnessTest, RapidSeek)
{
    ctrl_->open("test.mp4");
    ctrl_->play();
    ctrl_->seek(1'000'000);
    ctrl_->seek(5'000'000);
    ctrl_->seek(8'000'000);
    EXPECT_EQ(ctrl_->getPositionUs(), 8'000'000);
}
