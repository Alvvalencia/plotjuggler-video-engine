# Video in C++ for PlotJuggler — Research (March 2026)

## Requirements

- **Static video playback** with frame-accurate seeking and reverse playback
- **Live streaming** with lowest possible latency and bandwidth (robot-to-desktop)
- **Synchronized panel** — video tightly coupled with PlotJuggler's time-series timeline
- **Cross-platform** from day one (Linux, Windows, macOS)
- **Qt 6.8** — leverage the framework fully
- **Dependencies via Conan**
- **File formats**: MCAP + ROS bags + MP4

---

## Table of Contents

1. [C++ Video Libraries](#1-c-video-libraries)
2. [Qt 6.8 Multimedia Deep Dive](#2-qt-68-multimedia-deep-dive)
3. [Codecs](#3-codecs)
4. [Low-Latency Live Streaming Protocols](#4-low-latency-live-streaming-protocols)
5. [Frame-Accurate Seeking & Reverse Playback](#5-frame-accurate-seeking--reverse-playback)
6. [How Foxglove Handles Video](#6-how-foxglove-handles-video)
7. [How Rerun Handles Video](#7-how-rerun-handles-video)
8. [Robotics / AD Dataset Ecosystem](#8-robotics--ad-dataset-ecosystem)
9. [Conan Dependencies](#9-conan-dependencies)
10. [Comparative Analysis](#10-comparative-analysis)
11. [Recommended Architecture](#11-recommended-architecture)

---

## 1. C++ Video Libraries

### FFmpeg 8.0 — The Foundation

FFmpeg remains the gold standard for video decode/encode in C/C++. Version 8.0 (available on Conan as `ffmpeg/8.0.1`) brings native VVC decoding, Vulkan Video hardware decoding (the first cross-vendor HW decode API), and multi-threaded operation. It requires C11.

Hardware acceleration support covers all major platforms: VAAPI and NVDEC/CUDA on Linux, DXVA2/D3D11VA/D3D12VA on Windows, and VideoToolbox on macOS. The Vulkan Video path is a cross-platform alternative that is rapidly maturing.

Note: Qt 6.8 ships FFmpeg 7.1 internally for its `QMediaPlayer`, but for a custom decode pipeline we should use FFmpeg 8.0 directly.

**Links:**
- FFmpeg official: https://ffmpeg.org/
- FFmpeg 7.0 release notes: https://ffmpeg.org/download.html
- Vulkan Video in FFmpeg: https://wiki.videolan.org/Vulkan_Video

### GStreamer 1.26 / 1.28

GStreamer 1.26 (March 2025) added VVC, LCEVC, NVCODEC AV1 encode, and JPEG XS. GStreamer 1.28 (RC January 2026) adds Vulkan Video AV1/VP9 decode. However, the **C++ bindings (gstreamermm) are abandoned**; you must use the C API directly. The pipeline model is powerful but heavyweight for embedding in a Qt application.

**Not recommended** for PlotJuggler — FFmpeg provides more fine-grained control.

**Links:**
- GStreamer 1.26 release: https://gstreamer.freedesktop.org/releases/1.26/
- gstreamermm (abandoned): https://github.com/GNOME/gstreamermm

### libmpv

libmpv wraps FFmpeg with a clean C API and handles hardware acceleration, subtitles, seeking, and **reverse playback** out of the box. It is used by multiple Qt-based C++ video players. Good for quick integration but offers less control than raw FFmpeg. **Not available on Conan** — would need a custom recipe or git submodule.

**Links:**
- mpv: https://mpv.io/
- libmpv API: https://github.com/mpv-player/mpv/blob/master/libmpv/client.h

### QtAVPlayer

A Qt-integrated FFmpeg-based media player library (MIT license) with `stepForward()`/`stepBackward()`, frame-accurate seeking, and hardware acceleration on all platforms. **Not on Conan** and has no formal versioned releases — integrate from source. Worth investigating as a reference implementation or direct dependency.

**Links:**
- QtAVPlayer: https://github.com/valbok/QtAVPlayer

---

## 2. Qt 6.8 Multimedia Deep Dive

### What QMediaPlayer CAN Do

- Basic playback of all FFmpeg-supported formats
- Cross-platform HW acceleration (VAAPI, DXVA, VideoToolbox) via its internal FFmpeg 7.1
- `positionChanged()` in milliseconds; `QVideoFrame::startTime()/endTime()` in **microseconds**

### What QMediaPlayer CANNOT Do (Critical for PlotJuggler)

| Limitation | Detail |
|---|---|
| **No frame-accurate seeking** | Lands on nearest keyframe only |
| **No reverse playback** | Negative playback rates are explicitly unsupported |
| **No frame stepping** | No `stepForward()`/`stepBackward()` API |
| **No low-latency streaming** | RTSP has 2-3s latency; no native SRT support |
| **No external clock sync** | Cannot tie playback to an external timeline |
| **No custom decoder parameters** | No control over decode pipeline |

### What Qt 6.8 Gives Us for a Custom Pipeline

**`QAbstractVideoBuffer` is public again** (was private in Qt 6.0-6.7). This is the critical enabler for a custom FFmpeg pipeline integrated with Qt rendering.

#### QAbstractVideoBuffer API (Qt 6.8)

Three virtual methods to implement:
- `format()` — returns `QVideoFrameFormat` (pure virtual, called once at QVideoFrame construction)
- `map(QVideoFrame::MapMode)` — returns `MapData` struct with up to 4 planes of pointers/strides/sizes (pure virtual)
- `unmap()` — cleanup (virtual, default no-op)

Construction: `QVideoFrame(std::unique_ptr<QAbstractVideoBuffer>)` takes ownership of your buffer.

**Zero-copy GPU limitation**: The public API supports **CPU-accessible buffers only** via `map()`. True GPU texture passthrough requires `QHwVideoBuffer` (private/internal class with `textureHandle()` and `mapTextures()` virtuals). Qt's own VAAPI backend achieves zero-copy internally via `vaExportSurfaceHandle()` → DRM-PRIME DMA-BUF → `eglCreateImage()` → GL texture → `QRhiTexture::createFrom()`, but this pipeline is not publicly exposed.

#### QVideoSink::setVideoFrame()

- **Thread-safe**: uses internal `QMutex`; safe to call from a decode worker thread
- **No buffering/queue**: each call replaces the previous frame; push faster than render rate and intermediate frames are silently dropped
- **Connection**: `QVideoWidget::videoSink()` returns the sink. QVideoWidget wraps a QVideoWindow which renders via QRhi with automatic YUV-to-RGB shader selection
- **No frame pacing**: you must implement your own PTS-based scheduler

#### QVideoFrameInput (New in 6.8)

Designed for **recording, not playback**. Connects to `QMediaCaptureSession` → `QMediaRecorder`. Pull-mode via `readyToSendVideoFrame()` signal. FFmpeg backend only. **Not suitable for real-time display.**

#### QRhiWidget for Video

The best path for true zero-copy GPU rendering. Override `initialize()` and `render()`. Upload CPU frames via `QRhiResourceUpdateBatch::uploadTexture()`. For GPU interop, use `QRhiTexture::createFrom({nativeHandle, 0})` to wrap an existing GL/VK/D3D texture. You must implement YUV-to-RGB shaders yourself (Qt's built-in shaders are not public API).

#### QVideoFrameFormat Pixel Formats

30+ formats supported: all RGB8888 variants, YUV420P, YUV422P, NV12, NV21, P010, P016, UYVY, YUYV, Y8, Y16, etc. **Qt handles YUV-to-RGB conversion automatically** in the renderer via per-format fragment shaders (BT.601/709/2020 matrices, HDR PQ/HLG tone mapping). Push YUV frames directly — no manual conversion needed.

Key AVPixelFormat → Qt mappings:
| FFmpeg `AVPixelFormat` | Qt `QVideoFrameFormat::PixelFormat` |
|---|---|
| `AV_PIX_FMT_NV12` | `Format_NV12` |
| `AV_PIX_FMT_YUV420P` | `Format_YUV420P` |
| `AV_PIX_FMT_YUV422P` | `Format_YUV422P` |
| `AV_PIX_FMT_UYVY422` | `Format_UYVY` |
| `AV_PIX_FMT_YUYV422` | `Format_YUYV` |
| `AV_PIX_FMT_P010` | `Format_P010` |
| `AV_PIX_FMT_RGBA` | `Format_RGBA8888` |
| `AV_PIX_FMT_BGRA` | `Format_BGRA8888` |

Unmapped formats (RGB24, YUV444P, etc.) fall back to RGBA8888 or YUV420P via `swscale`.

#### Threading Model

- Decode on a worker thread, push via `setVideoFrame()` — **safe** due to internal mutex
- `QVideoFrame::startTime()/endTime()` (microseconds) are **metadata only** — QVideoSink does NOT use them for pacing
- No built-in frame scheduler — implement PTS-based timing yourself (timer + playback clock)
- No explicit vsync — rendering follows `requestUpdate()` cadence (~display refresh rate)

#### HW Acceleration Status

| Platform | Backend | Status |
|---|---|---|
| Linux | VAAPI | Supported (needs `QT_XCB_GL_INTEGRATION=xcb_egl`) |
| Linux (NVIDIA) | CUDA/NVDEC | Supported |
| Windows | DXVA2/D3D11VA/D3D12VA | Most stable platform |
| macOS/iOS | VideoToolbox | Supported |

QSV, VDPAU, DRM, OpenCL, Vulkan HW accel are listed but **not tested by Qt maintainers**.

**Links:**
- Qt 6.8 What's New: https://doc.qt.io/qt-6/whatsnew68.html
- QMediaPlayer: https://doc.qt.io/qt-6.8/qmediaplayer.html
- QVideoFrame: https://doc.qt.io/qt-6.8/qvideoframe.html
- QVideoSink: https://doc.qt.io/qt-6.8/qvideosink.html
- QAbstractVideoBuffer: https://doc.qt.io/qt-6/qabstractvideobuffer.html
- QVideoFrameInput: https://doc.qt.io/qt-6/qvideoframeinput.html
- QRhiWidget: https://doc.qt.io/qt-6/qrhiwidget.html
- Advanced FFmpeg Configuration: https://doc.qt.io/qt-6/advanced-ffmpeg-configuration.html
- QtAVPlayer: https://github.com/valbok/QtAVPlayer

---

## 3. Codecs

| Codec | Latency | Compression vs H.264 | HW Support | License | Status |
|---|---|---|---|---|---|
| **H.264/AVC** | Lowest | Baseline | Universal | Patented | Mature, best for live |
| **H.265/HEVC** | Low | ~40% better | Wide (GPU 2018+) | Patented | Mature |
| **AV1** | Medium | ~50% better | Growing (RTX 40+, RDNA 3+, Arc) | **Royalty-free** | Production-ready |
| **VVC/H.266** | High | ~60% better | Near-zero (Intel Lunar Lake only) | Patented | Too early — skip |

### Recommendations

- **Live streaming**: H.264 Baseline profile (no B-frames) for minimum latency. H.265 if hardware encode is available on the robot.
- **Static files**: Support H.264/H.265 for existing datasets; AV1 is the future (royalty-free, best compression).
- **VVC**: years from practical use with near-zero hardware support. Not worth targeting yet.

### Industry Consensus (Foxglove + Rerun)

Both Foxglove and Rerun agree on these constraints for robotics video:
- **No B-frames** — eliminates decode reordering, reduces latency
- **Annex B NAL units** for live streaming (not AVCC)
- **Self-contained keyframes** (SPS/PPS/VPS in each IDR frame)
- **AV1** as the future codec

### Software Codec Libraries

| Library | Purpose | Conan | Version |
|---|---|---|---|
| dav1d | AV1 decoder (fastest) | `dav1d` | 1.5.3 |
| SVT-AV1 | AV1 encoder (production) | `libsvtav1` | 2.2.1 (upstream: 4.0) |
| VVenC/VVdeC | VVC encoder/decoder (Fraunhofer, BSD) | Not on Conan | — |
| openh264 | H.264 decoder/encoder (Cisco, BSD) | Not on Conan | — |

**Links:**
- dav1d: https://code.videolan.org/videolan/dav1d
- SVT-AV1: https://gitlab.com/AOMediaCodec/SVT-AV1
- VVenC: https://github.com/fraunhoferhhi/vvenc
- VVdeC: https://github.com/fraunhoferhhi/vvdec
- openh264: https://github.com/cisco/openh264
- AV1 spec: https://aomedia.org/av1-features/

---

## 4. Low-Latency Live Streaming Protocols

| Protocol | Typical Latency | C++ Library | Conan Package | Notes |
|---|---|---|---|---|
| **Raw TCP/UDP** | <50ms | OS socket API | N/A | Simplest; no error recovery |
| **SRT** | 120-500ms (tunable) | libsrt (Haivision) | `srt/1.5.4` | Best balance of latency + reliability |
| **WebRTC** | 200-500ms | libdatachannel | `libdatachannel/0.24.0` | Complex but feature-rich; lightweight alternative to libwebrtc |
| **RIST** | ~500ms | librist | Not on Conan | Broadcast-grade, simpler than SRT for point-to-point |
| **MOQ (Media over QUIC)** | TBD | OpenMOQ consortium | N/A | Next-gen IETF protocol; **not production-ready** |
| **Low-latency HLS/DASH** | 2-6s | Various | — | Too high latency for robotics |

### Recommendation: SRT for Robot-to-Desktop

SRT (Secure Reliable Transport) via `libsrt` offers the best balance for PlotJuggler:
- Latency tunable down to ~120ms via `SRTO_LATENCY` socket option
- ARQ (Automatic Repeat Request) retransmission for reliability over lossy networks
- Simple C API, well-documented
- Growing ecosystem (OBS, FFmpeg, GStreamer all support SRT natively)
- Available on Conan (`srt/1.5.4`)

Payload format: raw H.264 Annex B NAL units over SRT, matching Foxglove and Rerun conventions.

**Links:**
- SRT Protocol: https://github.com/Haivision/srt
- SRT Technical Overview: https://github.com/Haivision/srt/blob/master/docs/features/live-streaming.md
- libdatachannel: https://github.com/paullouisageneau/libdatachannel
- librist: https://code.videolan.org/rist/librist
- MOQ (IETF): https://datatracker.ietf.org/wg/moq/about/

---

## 5. Frame-Accurate Seeking & Reverse Playback

### Seeking Strategy

1. **Build keyframe index at file open**: parse MP4 `stss` (Sync Sample) atom, or scan the stream to find all keyframe positions. Store as sorted vector of `{timestamp, byte_offset}`.
2. **Seek to keyframe**: use `av_seek_frame()` with `AVSEEK_FLAG_BACKWARD` — this reliably seeks to the keyframe before the target timestamp. **Prefer `av_seek_frame()` over `avformat_seek_file()`** — the latter ignores `AVSEEK_FLAG_BACKWARD` and its bounded-range semantics add complexity without benefit for this use case.
3. **Flush decoder**: call `avcodec_flush_buffers()` — **mandatory** after every seek. It resets decoder state, clears draining flags, and discards buffered B-frame references.
4. **Decode forward**: decode frames from keyframe to the exact target frame using `avcodec_send_packet()` / `avcodec_receive_frame()`, discarding unwanted frames.
5. **Cache decoded frames**: maintain a ring buffer of recently decoded frames for fast nearby scrubbing.

Three methods for finding GOP boundaries:
- `av_index_search_timestamp()` — works for indexed containers (MP4, MKV)
- `avformat_index_get_entry()` — available in FFmpeg 5.0+
- Full-scan fallback — for containers without indices (MPEG-TS, raw streams), pre-scan on file open

### Reverse Playback Algorithm (Detailed)

The core pattern: **seek to keyframe → decode forward through GOP → buffer all decoded frames → present in reverse PTS order → move to previous GOP**.

```
┌─────────────────────────────────────────────────────────┐
│                  Reverse Playback Pipeline               │
│                                                         │
│   GOP N-1              GOP N (current)       GOP N+1    │
│   ┌──────────┐         ┌──────────┐                     │
│   │ decoding │ ◄────── │ playing  │ ──────►             │
│   │ (bg thread)        │ (reverse)│                     │
│   └──────────┘         └──────────┘                     │
│                                                         │
│   Double-buffer: decode next-reverse GOP while          │
│   displaying current GOP in reverse                     │
└─────────────────────────────────────────────────────────┘
```

Steps:
1. Find the keyframe at or before the current position (using keyframe index)
2. `av_seek_frame(fmt_ctx, stream_idx, keyframe_pts, AVSEEK_FLAG_BACKWARD)`
3. `avcodec_flush_buffers(codec_ctx)` — **mandatory**
4. Decode all frames in the GOP using send/receive API, storing each `AVFrame` (sorted by PTS)
5. Present frames in **reverse PTS order** with correct timing intervals
6. When all frames in current GOP are displayed, move to the previous GOP and repeat
7. **Double-buffer**: while displaying current GOP in reverse, start decoding the previous GOP on a background thread

### Hardware Acceleration for Reverse Playback

HW decode **works** for the forward decode pass within each GOP. Transfer decoded frames from GPU to CPU via `av_hwframe_transfer_data()` (~0.5-2ms per 1080p frame). This is necessary because HW frame pools are too small (16-32 surfaces) to hold an entire GOP's worth of frames.

### Memory Budget

| Resolution | GOP Size | Pixel Format | Single GOP | Double-Buffered |
|---|---|---|---|---|
| 1080p | 30 frames (1s) | YUV420P | ~93 MB | **~186 MB** |
| 1080p | 30 frames (1s) | RGBA | ~249 MB | ~498 MB |
| 4K | 30 frames (1s) | YUV420P | ~372 MB | **~744 MB** |
| 4K | 60 frames (2s) | YUV420P | ~1.4 GB | ~2.8 GB |

**Keep frames in YUV420P** (1.5 bytes/pixel) rather than RGBA (4 bytes/pixel) — Qt handles YUV-to-RGB conversion in the renderer anyway.

### Performance Expectations

H.264 1080p software decodes at ~200-400 fps; a typical 30-frame GOP decodes in **~100ms** — well within the real-time budget at 30fps. GOPs of 250+ frames need HW decode or pre-buffering.

### Reference Implementations

**mpv** (`--play-dir=-`) is the most complete open-source implementation of reverse playback:
- 3-stage pipeline: backward demuxing → forward decoding with frame queuing → reversed output
- State machine in `demux/demux.c` with per-stream backward state tracking
- Frame queuing in `filters/f_decoder_wrapper.c`
- All timestamps negated internally so comparisons work unchanged
- Configurable: `--video-backward-overlap`, `--video-backward-batch`, `--demuxer-backward-playback-step` (default 60s)

Other implementations:
- **QtAVPlayer** (`stepBackward()`): single-frame stepping, not continuous reverse
- **libopenshot** (`FFmpegReader`): reverse via keyframe animation on speed
- **andrewtyw/ffmpeg-reverse-playback**: simple producer-consumer demo in C with Qt

### Alternative Approaches

- **All-intra transcode** (ProRes, DNxHR, H.264 `-g 1`): instant random access at 5-10x file size
- **LRU frame cache**: helps with scrubbing/rewinding to recently visited frames
- **Bidirectional buffer**: cache N frames in each direction from current position — enables instant direction changes (approach used by professional NLEs)

### Edge Cases

- **Variable GOPs**: never assume fixed size; always find actual keyframe boundaries per GOP
- **Open GOPs**: B-frames at GOP start may reference previous GOP. Fix by decoding one extra earlier GOP for reference frames (mpv's `--video-backward-overlap`)
- **B-frames**: `avcodec_receive_frame()` returns in display order, but always sort by PTS as a safety measure. Use `best_effort_timestamp` when PTS is `AV_NOPTS_VALUE`
- **No-index containers** (MPEG-TS, raw bitstreams): pre-scan to build a keyframe index on file open, or use iterative-rewind with 1-second decrements

### GOP Tradeoffs for Content Creation

| GOP Strategy | Seek Latency | File Size Impact | Use Case |
|---|---|---|---|
| Short GOPs (1s, ~30 frames) | Fast | +15-20% bitrate | Good default for robotics |
| Very short GOPs (0.5s) | Very fast | +25-30% bitrate | Editing workflows |
| All-intra (every frame is keyframe) | Instant | 5-10x file size | ProRes, DNxHR, MJPEG |
| Long GOPs (5s+) | Slow | Best compression | Archival only |

**Both Foxglove and Rerun ban B-frames**, which simplifies seeking: decode order matches display order, so no reordering buffer is needed.

### Timeline Synchronization (Fundamental)

The core integration between video and PlotJuggler's time-series plots:

- Each decoded `QVideoFrame` carries `startTime()/endTime()` in microseconds
- Map video timestamps to PlotJuggler's timeline coordinate system (which likely uses `double` seconds or nanosecond integers)
- **Plot drives video**: scrubbing/clicking the plot timeline → seek video to corresponding timestamp
- **Video drives plot**: playing video → update plot cursor position at each frame
- **Bidirectional**: either can drive the other; need a shared "current time" state

**Links:**
- FFmpeg seeking API: https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html
- FFmpeg seek.c source: https://ffmpeg.org/doxygen/trunk/seek_8c_source.html
- mpv reverse playback (issue #4000): https://github.com/mpv-player/mpv/issues/4000
- mpv backward playback commit: https://git.redxen.eu/RepoMirrors/mpv/commit/b9d351f02a3266b76256a90fc9c51f9d3cbf185d
- mpv demuxer system: https://deepwiki.com/mpv-player/mpv/3.1-demuxer-system
- mpv media pipeline: https://deepwiki.com/mpv-player/mpv/3-media-pipeline
- andrewtyw/ffmpeg-reverse-playback: https://github.com/andrewtyw/ffmpeg-reverse-playback
- QtAVPlayer: https://github.com/valbok/QtAVPlayer
- libopenshot FFmpegReader: https://www.openshot.org/static/files/libopenshot/FFmpegReader_8cpp_source.html
- FFmpeg HW context system: https://deepwiki.com/FFmpeg/FFmpeg/7.1-hardware-context-system
- Closed/Open GOP explained: https://ottverse.com/closed-gop-open-gop-idr/
- GOPs explained (AWS): https://aws.amazon.com/blogs/media/part-1-back-to-basics-gops-explained/
- ProRes vs DNxHD (frame.io): https://blog.frame.io/2017/02/15/choose-the-right-codec/

---

## 6. How Foxglove Handles Video

### Architecture

Three message types for visual data:
- **`foxglove.RawImage`** (= `sensor_msgs/Image`) — uncompressed pixels
- **`foxglove.CompressedImage`** (= `sensor_msgs/CompressedImage`) — per-frame JPEG/PNG/WebP/AVIF
- **`foxglove.CompressedVideo`** (= `foxglove_msgs/CompressedVideo`) — inter-frame H.264/H.265/VP9/AV1

The key design decision: **video is decomposed into individually-timestamped messages, one frame per MCAP message**. This is fundamentally different from a monolithic MP4/MKV file — each frame is a separate message with its own timestamp, enabling per-frame indexing and synchronization with other sensor data.

### CompressedVideo Schema

```
timestamp: Timestamp
frame_id: string
data: bytes        # compressed frame payload
format: string     # "h264", "h265", "vp9", "av1"
```

### Encoding Requirements

- H.264/H.265: must use **Annex B NAL units** (not AVCC format)
- H.264 keyframes (IDR): must include SPS NAL unit
- H.265 keyframes (IRAP): must include VPS/SPS/PPS NAL units
- AV1 keyframes: must include Sequence Header OBU
- **B-frames are unsupported** across all codecs
- Each message contains exactly enough data to decode one frame

### MCAP File Format

Structure: `Magic | Header | Data Section | [Summary] | [Summary Offset] | Footer | Magic`

Key constructs:
- **Schemas**: define message types (CompressedVideo, etc.)
- **Channels**: named streams (e.g., `/camera/front`)
- **Messages**: timestamped data packets
- **Chunks**: batched compressed messages (LZ4 or Zstd)
- **Message Index Records**: timestamp → byte offset within chunks
- **Chunk Index Records**: chunk location + timestamp range
- **Summary section** at file end: O(1) access to full index

MCAP is framework-agnostic, supports any serialization (Protobuf, FlatBuffers, CDR, JSON), guarantees forward compatibility, and was **adopted by NVIDIA as the default for Isaac ROS 3.0**.

### Live Streaming

The Foxglove SDK provides a WebSocket server (Rust/C++/Python) that runs on the robot. Clients connect and subscribe to channels. Each frame is sent as a CompressedVideo message. The original `foxglove/ws-protocol` repo was archived August 2025, replaced by `foxglove-sdk`.

### Seeking in MCAP

1. Read Summary section at EOF to load full index
2. Use Chunk Index to find the right chunk for a timestamp
3. Find the nearest keyframe at or before the target timestamp
4. Decode forward from that keyframe

Foxglove recommends keyframes every ~1 second for low seek latency.

**Links:**
- Foxglove: https://foxglove.dev/
- MCAP specification: https://mcap.dev/spec
- MCAP C++ library: https://github.com/foxglove/mcap (MIT, header-only)
- Foxglove SDK: https://github.com/foxglove/foxglove-sdk
- CompressedVideo schema: https://docs.foxglove.dev/docs/visualization/message-schemas/compressed-video
- Foxglove encoding guide: https://docs.foxglove.dev/docs/visualization/panels/image#compressed-video
- NVIDIA Isaac ROS + MCAP: https://developer.nvidia.com/isaac-ros

---

## 7. How Rerun Handles Video

### Architecture

Four ingestion methods:
- **`Image`** — raw uncompressed pixels
- **`EncodedImage`** — per-frame JPEG/PNG
- **`AssetVideo`** — MP4 files (only MP4 containers supported)
- **`VideoStream`** — live Annex B encoded packets (no container)

### Codec Support

- **AV1** is recommended (best compression + cross-platform compatibility)
- **H.264** preferred for live streaming (faster encoding)
- **H.265** supported but with caveats on some platforms

### Decoding Strategy

- **Native viewer**: built-in AV1 software decoder + external FFmpeg (>= 5.1) for H.264/H.265. FFmpeg is **not bundled** due to GPL licensing concerns.
- **Web viewer**: delegates to the browser's WebCodecs API for hardware-accelerated decoding.

### Storage (.rrd format)

- Arrow-encoded chunks (column-oriented, ~384 KiB target chunk size)
- Video data stored as binary blobs (`AssetVideo`) or per-packet rows (`VideoStream`)
- Auto-compacted on ingestion
- 100x write speed improvement since chunk redesign in v0.18

### Live Streaming (v0.24+)

- `VideoStream` archetype accepts raw Annex B encoded packets (no container needed)
- B-frames disabled for simplicity
- ~0.5s end-to-end latency
- Multi-sink architecture: simultaneous live viewing and .rrd recording

### Seeking

- `AssetVideo` uses `VideoFrameReference` with timestamps parsed by the `re_mp4` crate
- `VideoStream` requires decoding from nearest keyframe
- `VideoFrameReference` does not yet work with `VideoStream`

**Links:**
- Rerun: https://rerun.io/
- Rerun video documentation: https://rerun.io/docs/reference/video
- Rerun GitHub: https://github.com/rerun-io/rerun
- re_video crate: https://github.com/rerun-io/rerun/tree/main/crates/store/re_video
- VideoStream RFC: https://github.com/rerun-io/rerun/issues/8patients (search their GitHub issues for VideoStream)

---

## 8. Robotics / AD Dataset Ecosystem

### Traditional Datasets — Per-Frame Images

| Dataset | Image Format | Resolution | FPS | Container |
|---|---|---|---|---|
| **KITTI** | PNG | 1242x375 | 10 Hz | Raw files |
| **nuScenes** | JPEG | 1600x900 | 12 Hz | Raw files |
| **Waymo Open v1** | JPEG | 1920x1280 | 10 Hz | TFRecord |
| **Waymo Open v2** | JPEG | 1920x1280 | 10 Hz | Parquet |

All traditional datasets store images as individual per-frame files (PNG or JPEG), prioritizing random access over compression efficiency.

### The Shift to Video (2024-2025)

The industry is actively moving from per-frame images to compressed video:

- **NVIDIA PhysicalAI-AV (2025)**: Stores 1080p/30fps video as **MP4 files directly** — a major shift from image sequences.
- **HuggingFace LeRobot**: Uses AV1 in MP4 (libsvtav1, CRF 30, **GOP=2**) achieving **14% average file size** with equivalent ML training quality. The extremely short GOP (keyframe every 2 frames) prioritizes random access while still getting significant compression.
- **2024 Research Paper (ROS video encoding)**: AV1 at CRF 22 achieved a **56x size reduction** on a multi-camera robotics dataset with acceptable quality loss.

### Container Standards

**MCAP** is the emerging dominant container for robotics:
- Default storage backend in ROS 2 since Iron (2023)
- Append-only, chunk-indexed, supports LZ4/Zstd compression
- 10x faster than traditional rosbags for read/write
- Framework-agnostic (works outside ROS)
- Does **not** natively understand video codecs — treats frames as opaque binary messages
- Adopted by NVIDIA Isaac ROS 3.0

**Parquet** is becoming the metadata standard:
- Waymo v2, NVIDIA PhysicalAI-AV, and HuggingFace LeRobot all use Parquet for structured metadata
- Column-oriented, efficient for analytics queries
- HuggingFace Hub enables streaming access to Parquet datasets

**ROS bags** (legacy):
- rosbag1 (ROS 1): widely used legacy format, custom binary format
- rosbag2 (ROS 2): plugin-based storage, uses MCAP as default backend since 2023
- `embag` (Conan: `embag/0.0.42`) provides schema-free ROS 1 bag reading

### The Fundamental Tension

**Random access** (needed for timeline scrubbing, ML training) vs **temporal compression** (needed for storage/bandwidth efficiency).

Current solutions:
- **Short GOPs**: LeRobot uses GOP=2 (keyframe every 2 frames) — nearly random access with decent compression
- **Chunk indexing**: MCAP and Rerun .rrd provide fast lookup by timestamp
- **Dual storage**: Compressed video file + separate timestamp/metadata index (Parquet)
- **All-intra encoding**: Maximum random access at 5-10x file size (MJPEG, ProRes)

**Links:**
- MCAP specification: https://mcap.dev/spec
- MCAP C++ library (Conan: `mcap/2.1.1`): https://github.com/foxglove/mcap
- KITTI dataset: https://www.cvlibs.net/datasets/kitti/
- nuScenes: https://www.nuscenes.org/
- Waymo Open Dataset: https://waymo.com/open/
- HuggingFace LeRobot: https://github.com/huggingface/lerobot
- NVIDIA PhysicalAI-AV: https://developer.nvidia.com/physical-ai
- embag (ROS 1 reader): https://github.com/embeddedartistry/embag

---

## 9. Conan Dependencies

### Available on Conan Center

| Library | Conan Package | Latest Version | Purpose |
|---|---|---|---|
| FFmpeg | `ffmpeg` | **8.0.1** (Feb 2026) | Core decode/encode engine |
| SRT | `srt` | **1.5.4** (Apr 2025) | Low-latency live streaming |
| libdatachannel | `libdatachannel` | **0.24.0** (Dec 2025) | WebRTC (if needed later) |
| dav1d | `dav1d` | **1.5.3** (Jan 2026) | AV1 decoder (fastest available) |
| SVT-AV1 | `libsvtav1` | **2.2.1** (Feb 2025) | AV1 encoder (**upstream at 4.0, Conan lagging**) |
| MCAP | `mcap` | **2.1.1** (Oct 2025) | MCAP file reading (header-only, MIT) |

### Not on Conan (need custom recipe or source integration)

| Library | Source | Notes |
|---|---|---|
| QtAVPlayer | https://github.com/valbok/QtAVPlayer | No versioned releases; MIT license |
| libmpv | https://github.com/mpv-player/mpv | LGPL; complex build |
| rosbag2 | https://github.com/ros2/rosbag2 | ROS 2 build system only (colcon/ament) |
| librist | https://code.videolan.org/rist/librist | Broadcast-grade RIST protocol |

**Links:**
- Conan Center: https://conan.io/center
- FFmpeg on Conan: https://conan.io/center/recipes/ffmpeg
- SRT on Conan: https://conan.io/center/recipes/srt
- libdatachannel on Conan: https://conan.io/center/recipes/libdatachannel
- dav1d on Conan: https://conan.io/center/recipes/dav1d
- libsvtav1 on Conan: https://conan.io/center/recipes/libsvtav1
- mcap on Conan: https://conan.io/center/recipes/mcap

---

## 10. Comparative Analysis

### Foxglove vs Rerun vs PlotJuggler Target

| Aspect | Foxglove | Rerun | PlotJuggler Target |
|---|---|---|---|
| **Video in files** | Per-frame msgs in MCAP | MP4 (AssetVideo) or per-packet .rrd | MCAP + MP4 + ROS bags |
| **Live video** | CompressedVideo over WebSocket | VideoStream (Annex B packets) | SRT (lowest latency) |
| **Codecs** | H.264, H.265, VP9, AV1 | AV1 (rec'd), H.264, H.265 | H.264 + H.265 + AV1 |
| **Seeking** | MCAP index → keyframe → decode | MP4 index → keyframe → decode | Frame-accurate + reverse |
| **B-frames** | Explicitly banned | Explicitly banned | Banned |
| **Decoding** | Browser/Electron native | FFmpeg / WebCodecs | FFmpeg 8.0 (custom pipeline) |
| **Rendering** | Web canvas | Web canvas / egui | Qt 6.8 QVideoSink / QRhiWidget |
| **Timeline sync** | MCAP timestamps | Arrow timestamps | QVideoFrame µs timestamps ↔ plot axis |
| **HW accel** | Browser-delegated | Browser/FFmpeg-delegated | Direct VAAPI/NVDEC/DXVA/VideoToolbox |
| **Platform** | Web (Electron) | Web + Native (Rust) | Native C++ (Qt 6.8) |

### What PlotJuggler Can Do Better

- **Native C++ performance** — no browser/Electron overhead
- **Frame-accurate seeking + reverse playback** — neither Foxglove nor Rerun fully supports this
- **Tight timeline integration** — video scrubbing synchronized with time-series plots (both can drive each other)
- **Multiple format support** — MCAP + ROS bags + standard video files in one tool
- **Cross-platform HW acceleration** — direct FFmpeg HW decode, not browser-delegated

---

## 11. Recommended Architecture

```
                    ┌──────────────────────────────────────────┐
                    │            PlotJuggler Core               │
                    │         (shared timeline state)           │
                    └──────────┬───────────────┬───────────────┘
                               │               │
                    ┌──────────▼──────┐ ┌──────▼───────────────┐
                    │  Time-Series    │ │    Video Panel        │
                    │  Plot Panel     │ │                       │
                    │  (existing)     │ │  QVideoWidget         │
                    │                 │ │    └─ QVideoSink      │
                    └─────────────────┘ │         ▲             │
                                        │         │ setVideoFrame()
                                        │  ┌──────┴──────────┐ │
                                        │  │ Frame Scheduler  │ │
                                        │  │ (PTS-based)      │ │
                                        │  └──────▲───────────┘ │
                                        └─────────┼─────────────┘
                                                  │
                                     ┌────────────┴────────────┐
                                     │     Decode Pipeline      │
                                     │  (worker thread)         │
                                     │                          │
                                     │  FFmpeg 8.0              │
                                     │  ├─ avformat (demux)     │
                                     │  ├─ avcodec (decode)     │
                                     │  │   └─ HW accel         │
                                     │  └─ Frame ring buffer    │
                                     └────────────▲─────────────┘
                                                  │
                              ┌────────────────────┼────────────────┐
                              │                    │                │
                     ┌────────┴───────┐  ┌────────┴──────┐  ┌─────┴──────┐
                     │  MP4/MKV       │  │  MCAP         │  │  SRT Live  │
                     │  (avformat)    │  │  (mcap lib)   │  │  (libsrt)  │
                     └────────────────┘  └───────────────┘  └────────────┘
```

### Key Design Decisions

1. **Custom FFmpeg pipeline** (not QMediaPlayer) for frame-accurate seeking, reverse playback, and low-latency streaming
2. **Qt 6.8 QAbstractVideoBuffer** to bridge FFmpeg decoded frames into Qt's rendering (thread-safe, YUV-to-RGB handled by Qt)
3. **SRT** for live streaming with H.264 Annex B payload
4. **MCAP** library for reading robotics data files; FFmpeg's avformat for standard video files
5. **Shared timeline state** between plot panels and video panel — either can drive the other
6. **Frame ring buffer** for fast nearby scrubbing and GOP-based reverse playback
7. **Worker thread decode** pushing frames to UI thread via QVideoSink (thread-safe)
