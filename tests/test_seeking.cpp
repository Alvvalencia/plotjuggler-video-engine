#include <gtest/gtest.h>
#include "../src/FFmpegBackend.h"
#include "VideoEngine/VideoController.h"
#include <QCoreApplication>
#include <QVideoSink>
#include <QEventLoop>
#include <QTimer>
#include <filesystem>

using namespace videoengine;

namespace {

std::string testVideoPath()
{
    return (std::filesystem::path(__FILE__).parent_path()
            / "data" / "test_coarse.mp4").string();
}

// Tolerance: 1 frame at 30 FPS = 33,333 µs (per EVALUATION.md Phase 3)
constexpr int64_t kFrameTolerance = 33'333;

} // namespace

class SeekingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        backend_ = std::make_unique<FFmpegBackend>();
        ASSERT_TRUE(backend_->open(QString::fromStdString(testVideoPath())));
        sink_ = std::make_unique<QVideoSink>();
        backend_->connectToSink(sink_.get());
    }

    std::unique_ptr<FFmpegBackend> backend_;
    std::unique_ptr<QVideoSink> sink_;
};

// SK-01: Seek to second 0 — position within 1 frame of 0
TEST_F(SeekingTest, SeekToZero)
{
    backend_->seek(0);
    EXPECT_NEAR(backend_->getPositionUs(), 0, kFrameTolerance);
}

// SK-02: Seek to second 15 — position within 1 frame of 15s
TEST_F(SeekingTest, SeekToMiddle)
{
    backend_->seek(15'000'000);
    EXPECT_NEAR(backend_->getPositionUs(), 15'000'000, kFrameTolerance);
}

// SK-03: Seek to second 29 — position within 1 frame of 29s
TEST_F(SeekingTest, SeekToNearEnd)
{
    backend_->seek(29'000'000);
    EXPECT_NEAR(backend_->getPositionUs(), 29'000'000, kFrameTolerance);
}

// SK-04: Seek during playback — position updates correctly
TEST_F(SeekingTest, SeekDuringPlayback)
{
    backend_->play();

    QEventLoop loop;
    QTimer::singleShot(200, &loop, &QEventLoop::quit);
    loop.exec();

    backend_->seek(20'000'000);
    EXPECT_NEAR(backend_->getPositionUs(), 20'000'000, kFrameTolerance);

    backend_->stop();
}

// SK-05: Step forward from beginning — position advances by ~1 frame
TEST_F(SeekingTest, StepForward)
{
    // Seek to start to establish position
    backend_->seek(0);
    int64_t pos0 = backend_->getPositionUs();

    EXPECT_TRUE(backend_->stepForward());
    int64_t pos1 = backend_->getPositionUs();
    EXPECT_GT(pos1, pos0);
    EXPECT_NEAR(pos1 - pos0, kFrameTolerance, kFrameTolerance);
}

// SK-06: Step backward — position goes back by ~1 frame
TEST_F(SeekingTest, StepBackward)
{
    // Seek to 5 seconds, then step backward
    backend_->seek(5'000'000);
    int64_t posBefore = backend_->getPositionUs();

    EXPECT_TRUE(backend_->stepBackward());
    int64_t posAfter = backend_->getPositionUs();
    EXPECT_LT(posAfter, posBefore);
    EXPECT_NEAR(posBefore - posAfter, kFrameTolerance, kFrameTolerance);
}

// SK-07: Multiple seeks to different positions
TEST_F(SeekingTest, MultipleSeeks)
{
    int64_t targets[] = {5'000'000, 25'000'000, 1'000'000, 28'000'000, 0};
    for (int64_t t : targets) {
        backend_->seek(t);
        EXPECT_NEAR(backend_->getPositionUs(), t, kFrameTolerance)
            << "Failed for target=" << t;
    }
}

// SK-08: VideoController + step integration
TEST_F(SeekingTest, ControllerStepIntegration)
{
    auto backend = std::make_unique<FFmpegBackend>();
    ASSERT_TRUE(backend->open(QString::fromStdString(testVideoPath())));

    QVideoSink sink;
    backend->connectToSink(&sink);

    VideoController ctrl(std::move(backend));
    ASSERT_TRUE(ctrl.open(QString::fromStdString(testVideoPath())));
    ctrl.seek(5'000'000);

    // Step forward puts us in PAUSED state
    EXPECT_TRUE(ctrl.stepForward());
    EXPECT_EQ(ctrl.getState(), PlaybackState::PAUSED);

    EXPECT_TRUE(ctrl.stepBackward());
    EXPECT_EQ(ctrl.getState(), PlaybackState::PAUSED);
}
