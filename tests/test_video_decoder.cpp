#include <gtest/gtest.h>
#include "../src/sources/file_video_source.h"
#include "../src/core/video_decoder.h"
#include "../src/core/decoded_frame.h"
#include "../src/qt/ffmpeg_video_buffer.h"
#include <QVideoFrameFormat>
#include <filesystem>

extern "C" {
#include <libavutil/pixfmt.h>
}

using namespace videoengine;

namespace {

std::string testVideoPath()
{
    // Resolve relative to the test executable's source location
    auto path = std::filesystem::path(__FILE__).parent_path() / "data" / "test_coarse.mp4";
    return path.string();
}

} // namespace

// DEC-01: FileVideoSource::open() valid MP4
TEST(VideoDecoderTest, FileSourceOpenValid)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));

    auto info = source.streamInfo();
    EXPECT_EQ(info.width, 1920);
    EXPECT_EQ(info.height, 1080);
    EXPECT_EQ(info.codec, Codec::H264);
    EXPECT_NEAR(info.fps, 30.0, 0.5);
    EXPECT_TRUE(source.isOpen());
}

// DEC-02: FileVideoSource::open() nonexistent file
TEST(VideoDecoderTest, FileSourceOpenInvalid)
{
    FileVideoSource source;
    EXPECT_FALSE(source.open("/tmp/nonexistent_video_42.mp4"));
    EXPECT_FALSE(source.isOpen());
}

// DEC-03: FileVideoSource::readPacket() returns valid packet
TEST(VideoDecoderTest, FileSourceReadPacket)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));

    auto pkt = source.readPacket();
    ASSERT_TRUE(pkt.has_value());
    EXPECT_TRUE(pkt->isValid());
    EXPECT_GE(pkt->pts(), 0);
}

// DEC-04: VideoDecoder::open() with valid codec parameters
TEST(VideoDecoderTest, DecoderOpenValid)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));

    VideoDecoder decoder;
    EXPECT_TRUE(decoder.open(source.codecParameters()));
    EXPECT_TRUE(decoder.isOpen());
}

// DEC-05: VideoDecoder::decode() produces frame from first keyframe
TEST(VideoDecoderTest, DecodeProducesFrame)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));

    VideoDecoder decoder;
    ASSERT_TRUE(decoder.open(source.codecParameters()));

    // Read packets until we get a decoded frame
    std::optional<DecodedFrame> frame;
    for (int i = 0; i < 100 && (!frame || !frame->isValid()); ++i) {
        auto pkt = source.readPacket();
        if (!pkt) break;
        frame = decoder.decode(pkt->raw());
    }

    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(frame->isValid());
    EXPECT_EQ(frame->width(), 1920);
    EXPECT_EQ(frame->height(), 1080);
}

// DEC-06: DecodedFrame RAII copy + destroy — no ASan errors
TEST(VideoDecoderTest, DecodedFrameCopy)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));

    VideoDecoder decoder;
    ASSERT_TRUE(decoder.open(source.codecParameters()));

    std::optional<DecodedFrame> frame;
    for (int i = 0; i < 100 && (!frame || !frame->isValid()); ++i) {
        auto pkt = source.readPacket();
        if (!pkt) break;
        frame = decoder.decode(pkt->raw());
    }
    ASSERT_TRUE(frame.has_value());

    // Copy
    DecodedFrame copy(*frame);
    EXPECT_EQ(copy.width(), frame->width());
    EXPECT_EQ(copy.height(), frame->height());
    EXPECT_TRUE(copy.isValid());

    // Original still valid after copy
    EXPECT_TRUE(frame->isValid());

    // Destroy copy, original still valid
    { DecodedFrame tmp(std::move(copy)); }
    EXPECT_TRUE(frame->isValid());
}

// DEC-07: FFmpegVideoBuffer::format() maps YUV420P correctly
TEST(VideoDecoderTest, VideoBufferFormat)
{
    auto qtFmt = FFmpegVideoBuffer::toQtPixelFormat(AV_PIX_FMT_YUV420P);
    EXPECT_EQ(qtFmt, QVideoFrameFormat::Format_YUV420P);

    qtFmt = FFmpegVideoBuffer::toQtPixelFormat(AV_PIX_FMT_NV12);
    EXPECT_EQ(qtFmt, QVideoFrameFormat::Format_NV12);

    qtFmt = FFmpegVideoBuffer::toQtPixelFormat(AV_PIX_FMT_RGBA);
    EXPECT_EQ(qtFmt, QVideoFrameFormat::Format_RGBA8888);

    // Unknown format
    qtFmt = FFmpegVideoBuffer::toQtPixelFormat(-999);
    EXPECT_EQ(qtFmt, QVideoFrameFormat::Format_Invalid);
}

// DEC-08: FFmpegVideoBuffer::map() returns valid plane data
TEST(VideoDecoderTest, VideoBufferMap)
{
    FileVideoSource source;
    ASSERT_TRUE(source.open(testVideoPath()));

    VideoDecoder decoder;
    ASSERT_TRUE(decoder.open(source.codecParameters()));

    std::optional<DecodedFrame> frame;
    for (int i = 0; i < 100 && (!frame || !frame->isValid()); ++i) {
        auto pkt = source.readPacket();
        if (!pkt) break;
        frame = decoder.decode(pkt->raw());
    }
    ASSERT_TRUE(frame.has_value());

    FFmpegVideoBuffer buffer(std::move(*frame));

    // Check format
    auto fmt = buffer.format();
    EXPECT_EQ(fmt.pixelFormat(), QVideoFrameFormat::Format_YUV420P);
    EXPECT_EQ(fmt.frameWidth(), 1920);
    EXPECT_EQ(fmt.frameHeight(), 1080);

    // Map and verify planes
    auto mapData = buffer.map(QVideoFrame::ReadOnly);
    EXPECT_EQ(mapData.planeCount, 3); // YUV420P has 3 planes
    for (int i = 0; i < mapData.planeCount; ++i) {
        EXPECT_NE(mapData.data[i], nullptr);
        EXPECT_GT(mapData.bytesPerLine[i], 0);
        EXPECT_GT(mapData.dataSize[i], 0);
    }

    buffer.unmap();
}
