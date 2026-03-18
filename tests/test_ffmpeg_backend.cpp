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
    auto path = std::filesystem::path(__FILE__).parent_path() / "data" / "test_coarse.mp4";
    return path.string();
}

} // namespace

// FB-01: FFmpegBackend::open() valid MP4
TEST(FFmpegBackendTest, OpenValid)
{
    FFmpegBackend backend;
    ASSERT_TRUE(backend.open(QString::fromStdString(testVideoPath())));
    EXPECT_GT(backend.getDurationUs(), 0);
    EXPECT_EQ(backend.getState(), PlaybackState::LOADED);
}

// FB-02: FFmpegBackend::open() nonexistent file
TEST(FFmpegBackendTest, OpenInvalid)
{
    FFmpegBackend backend;
    EXPECT_FALSE(backend.open("/tmp/nonexistent_video_42.mp4"));
}

// FB-03: open → play → wait 1s → pause → getPositionUs() > 0
TEST(FFmpegBackendTest, PlayPausePosition)
{
    FFmpegBackend backend;
    ASSERT_TRUE(backend.open(QString::fromStdString(testVideoPath())));

    QVideoSink sink;
    backend.connectToSink(&sink);
    backend.play();

    // Let Qt process events for 1 second
    QEventLoop loop;
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);
    loop.exec();

    backend.pause();

    int64_t pos = backend.getPositionUs();
    EXPECT_GT(pos, 0);
    EXPECT_LT(pos, backend.getDurationUs());
    EXPECT_EQ(backend.getState(), PlaybackState::PAUSED);
}

// FB-04: VideoController + FFmpegBackend: full state transitions + clean shutdown
TEST(FFmpegBackendTest, ControllerIntegration)
{
    auto backend = std::make_unique<FFmpegBackend>();
    auto* raw = backend.get();

    VideoController ctrl(std::move(backend));
    EXPECT_EQ(ctrl.getState(), PlaybackState::IDLE);

    ASSERT_TRUE(ctrl.open(QString::fromStdString(testVideoPath())));
    EXPECT_EQ(ctrl.getState(), PlaybackState::LOADED);

    QVideoSink sink;
    ctrl.connectToSink(&sink);

    EXPECT_TRUE(ctrl.play());
    EXPECT_EQ(ctrl.getState(), PlaybackState::PLAYING);

    // Brief playback
    QEventLoop loop;
    QTimer::singleShot(200, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_TRUE(ctrl.stop());
    EXPECT_EQ(ctrl.getState(), PlaybackState::STOPPED);
    // Clean destruction happens in ctrl's destructor — must not hang or crash
}
