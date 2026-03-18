#include <gtest/gtest.h>
#include "VideoEngine/VideoController.h"
#include "MockVideoBackend.h"

using namespace videoengine;
using namespace videoengine::testing;

class StateTransitionTest : public ::testing::Test {
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

// UT-ST-01: play after open → PLAYING
TEST_F(StateTransitionTest, PlayAfterOpen)
{
    ctrl_->open("test.mp4");
    EXPECT_TRUE(ctrl_->play());
    EXPECT_EQ(ctrl_->getState(), PlaybackState::PLAYING);
}

// UT-ST-02: pause during PLAYING → PAUSED
TEST_F(StateTransitionTest, PauseDuringPlaying)
{
    ctrl_->open("test.mp4");
    ctrl_->play();
    EXPECT_TRUE(ctrl_->pause());
    EXPECT_EQ(ctrl_->getState(), PlaybackState::PAUSED);
}

// UT-ST-03: play after pause → PLAYING
TEST_F(StateTransitionTest, PlayAfterPause)
{
    ctrl_->open("test.mp4");
    ctrl_->play();
    ctrl_->pause();
    EXPECT_TRUE(ctrl_->play());
    EXPECT_EQ(ctrl_->getState(), PlaybackState::PLAYING);
}

// UT-ST-04: stop from PLAYING → STOPPED
TEST_F(StateTransitionTest, StopFromPlaying)
{
    ctrl_->open("test.mp4");
    ctrl_->play();
    EXPECT_TRUE(ctrl_->stop());
    EXPECT_EQ(ctrl_->getState(), PlaybackState::STOPPED);
}

// UT-ST-05: play from IDLE → false
TEST_F(StateTransitionTest, PlayFromIdle)
{
    EXPECT_FALSE(ctrl_->play());
    EXPECT_EQ(ctrl_->getState(), PlaybackState::IDLE);
}

// UT-ST-06: pause when PAUSED → no-op, true
TEST_F(StateTransitionTest, PauseWhenAlreadyPaused)
{
    ctrl_->open("test.mp4");
    ctrl_->play();
    ctrl_->pause();
    EXPECT_TRUE(ctrl_->pause());
    EXPECT_EQ(ctrl_->getState(), PlaybackState::PAUSED);
}

// UT-ST-07: play after stop → PLAYING
TEST_F(StateTransitionTest, PlayAfterStop)
{
    ctrl_->open("test.mp4");
    ctrl_->play();
    ctrl_->stop();
    EXPECT_TRUE(ctrl_->play());
    EXPECT_EQ(ctrl_->getState(), PlaybackState::PLAYING);
}
