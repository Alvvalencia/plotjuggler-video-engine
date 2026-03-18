#include <gtest/gtest.h>
#include "VideoEngine/VideoController.h"
#include "MockVideoBackend.h"

using namespace videoengine;
using namespace videoengine::testing;

class VideoSelectionTest : public ::testing::Test {
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

// UT-SEL-01: open valid path → LOADED
TEST_F(VideoSelectionTest, OpenValidPath)
{
    EXPECT_TRUE(ctrl_->open("test_video.mp4"));
    EXPECT_EQ(ctrl_->getState(), PlaybackState::LOADED);
}

// UT-SEL-02: open("") → false, stays IDLE
TEST_F(VideoSelectionTest, OpenEmptyPath)
{
    EXPECT_FALSE(ctrl_->open(""));
    EXPECT_EQ(ctrl_->getState(), PlaybackState::IDLE);
}

// UT-SEL-03: open(invalid, mock returns false) → false, stays IDLE
TEST_F(VideoSelectionTest, OpenInvalidPath)
{
    mock_->setOpenResult(false);
    EXPECT_FALSE(ctrl_->open("nonexistent.mp4"));
    EXPECT_EQ(ctrl_->getState(), PlaybackState::IDLE);
}

// UT-SEL-04: open when already LOADED → replaces, stays LOADED
TEST_F(VideoSelectionTest, OpenReplacesLoaded)
{
    EXPECT_TRUE(ctrl_->open("first.mp4"));
    EXPECT_EQ(ctrl_->getState(), PlaybackState::LOADED);

    EXPECT_TRUE(ctrl_->open("second.mp4"));
    EXPECT_EQ(ctrl_->getState(), PlaybackState::LOADED);
}
