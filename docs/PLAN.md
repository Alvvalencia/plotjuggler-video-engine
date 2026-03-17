# PlotJuggler Video Engine — Execution Plan

## Context

PlotJuggler needs video support: file playback (forward/reverse, frame-accurate seeking) and live streaming (low-latency, robot-to-desktop). The architecture must be unified from the start — both file and stream sources produce timestamped compressed packets, and everything downstream (decode, buffer, schedule, render) is shared. Built as a standalone C++20 library + demo app, later integrated into PlotJuggler.

See `RESEARCH.md` for full technology research.

---

## Architecture Overview

```
┌────────────────────────────────────────────────────────────────────┐
│                         VideoEngine                                │
│                                                                    │
│  ┌──────────────────┐                                              │
│  │   VideoSource     │ ← abstract interface                        │
│  │   (thread: source)│                                              │
│  └────────┬─────────┘                                              │
│           │ VideoPacket{timestamp, data, is_keyframe, codec}       │
│           ▼                                                        │
│  ┌──────────────────┐                                              │
│  │   PacketQueue     │ ← thread-safe bounded SPSC queue            │
│  └────────┬─────────┘                                              │
│           │                                                        │
│           ▼                                                        │
│  ┌──────────────────┐                                              │
│  │   VideoDecoder    │ ← FFmpeg avcodec wrapper                    │
│  │   (thread: decode)│    HW accel configurable per-platform       │
│  └────────┬─────────┘                                              │
│           │ DecodedFrame{timestamp, AVFrame*, format}              │
│           ▼                                                        │
│  ┌──────────────────┐                                              │
│  │   FrameBuffer     │ ← ring buffer + LRU cache                  │
│  │                    │   GOP-aware for reverse playback            │
│  └────────┬─────────┘                                              │
│           │                                                        │
│           ▼                                                        │
│  ┌──────────────────┐                                              │
│  │ PlaybackController│ ← state machine: play/pause/seek/reverse   │
│  │   (thread: UI)    │   PTS-based frame scheduling                │
│  └────────┬─────────┘                                              │
│           │ QVideoFrame (via FFmpegVideoBuffer)                    │
│           ▼                                                        │
│  ┌──────────────────┐                                              │
│  │  QVideoSink       │ ← Qt 6.8 rendering (YUV→RGB automatic)    │
│  │  → QVideoWidget   │                                             │
│  └──────────────────┘                                              │
└────────────────────────────────────────────────────────────────────┘
```

### Threading Model

```
Source Thread ──► PacketQueue ──► Decode Thread ──► FrameBuffer ──► UI Thread (scheduler + render)
     │                                                                    │
     │ (I/O bound:                                                        │ (timer-driven:
     │  file read or                                                      │  QVideoSink::setVideoFrame()
     │  SRT recv)                                                         │  is thread-safe)
```

### VideoSource — The Unifying Abstraction

Both file and stream sources implement the same interface. The only difference is capability flags.

```cpp
class VideoSource {
public:
    virtual bool open(const std::string& uri) = 0;
    virtual void close() = 0;

    // Packet reading — called by source thread
    // Returns nullopt on EOF (file) or disconnect (stream)
    virtual std::optional<VideoPacket> readPacket() = 0;

    // Capability flags
    virtual bool isSeekable() const = 0;   // file: true, live: false
    virtual bool isLive() const = 0;       // file: false, live: true

    // File-only (return nullopt/false for live)
    virtual std::optional<Duration> duration() const = 0;
    virtual bool seekTo(Timestamp ts) = 0;
    virtual const KeyframeIndex& keyframeIndex() const = 0;

    // Stream metadata
    virtual VideoStreamInfo streamInfo() const = 0;
};
```

| Source | isSeekable | isLive | duration | seekTo | readPacket |
|--------|-----------|--------|----------|--------|------------|
| FileVideoSource | true | false | file length | seeks in file | reads from avformat |
| McapVideoSource | true | false | time range | jumps via MCAP index | reads MCAP messages |
| SrtVideoSource | false | true | nullopt | no-op | blocks on SRT recv |

---

## Directory Structure

```
video/
├── CMakeLists.txt
├── conanfile.py
├── RESEARCH.md
├── PLAN.md
├── src/
│   ├── core/
│   │   ├── types.h                     # Timestamp, Duration, Codec enum
│   │   ├── video_packet.h              # Compressed packet
│   │   ├── decoded_frame.h             # AVFrame wrapper (RAII)
│   │   ├── video_source.h             # Abstract source interface
│   │   ├── video_decoder.h / .cpp      # FFmpeg avcodec wrapper
│   │   ├── packet_queue.h             # Thread-safe SPSC bounded queue
│   │   ├── frame_buffer.h / .cpp       # Ring buffer + LRU + GOP cache
│   │   ├── keyframe_index.h / .cpp     # Sorted keyframe timestamp index
│   │   └── playback_controller.h/.cpp  # State machine + PTS scheduler
│   ├── sources/
│   │   ├── file_video_source.h / .cpp  # avformat-based (MP4, MKV, etc.)
│   │   ├── mcap_video_source.h / .cpp  # MCAP reader
│   │   └── srt_video_source.h / .cpp   # SRT receiver
│   ├── qt/
│   │   ├── ffmpeg_video_buffer.h/.cpp  # QAbstractVideoBuffer subclass
│   │   ├── video_widget.h / .cpp       # QWidget: video display + controls
│   │   └── timeline_bridge.h / .cpp    # Abstract sync interface for PJ
│   └── demo/
│       └── main.cpp                    # Demo application
└── tests/
    ├── CMakeLists.txt
    ├── test_video_decoder.cpp
    ├── test_frame_buffer.cpp
    ├── test_packet_queue.cpp
    └── test_keyframe_index.cpp
```

---

## Phase 1: Project Scaffold + Core Types

**Goal**: CMake+Conan project that compiles and links FFmpeg + Qt 6.8. Core data types defined. A "hello world" that decodes one frame from an MP4 and shows it in a QVideoWidget.

### 1.1 Project setup
- `conanfile.py` with dependencies: `ffmpeg/8.0.1`, `qt/6.8.x`
- `CMakeLists.txt` with C++20, Qt6 find_package, FFmpeg find_package
- Build and verify on Linux first (CI later)

### 1.2 Core data types (`src/core/`)
- `types.h`: `Timestamp` (int64_t microseconds), `Duration`, `Codec` enum (H264, H265, AV1), `PlaybackState` enum
- `video_packet.h`: `VideoPacket{timestamp, data (std::vector<uint8_t>), is_keyframe, codec, stream_index}`
- `decoded_frame.h`: RAII wrapper around `AVFrame*` with ref-counting (`av_frame_ref`/`av_frame_unref`). Exposes width, height, pixel format, timestamp, plane data pointers.

### 1.3 FFmpegVideoBuffer (`src/qt/`)
- Subclass `QAbstractVideoBuffer`
- `format()`: map `AVPixelFormat` → `QVideoFrameFormat::PixelFormat`
- `map()`: return `AVFrame->data[]` pointers and `linesize[]` as `MapData`
- `unmap()`: no-op (frame owns the data)
- Constructor takes `DecodedFrame` by value (shared ref-counted AVFrame)

### 1.4 Smoke test
- `demo/main.cpp`: open an MP4 with `avformat_open_input`, read one video packet, decode it with `avcodec_send_packet`/`avcodec_receive_frame`, wrap in `FFmpegVideoBuffer`, create `QVideoFrame`, push to `QVideoWidget::videoSink()->setVideoFrame()`. Window shows one frame.

### Verification
- `cmake --build` succeeds on Linux/macOS/Windows
- Demo app opens, shows a single video frame, exits

### Conan deps this phase
- `ffmpeg/8.0.1`
- Qt 6.8 (system or Conan)

---

## Phase 2: File Playback Pipeline

**Goal**: Play an MP4 file forward with play/pause. Threaded pipeline with packet queue.

### 2.1 VideoSource interface (`src/core/video_source.h`)
- Define the abstract interface (as shown in Architecture above)
- `VideoStreamInfo`: codec, width, height, pixel_format, fps, time_base

### 2.2 FileVideoSource (`src/sources/`)
- `open(path)`: `avformat_open_input` + `avformat_find_stream_info` + find best video stream
- `readPacket()`: `av_read_frame()`, wrap in `VideoPacket`
- `seekTo()`: `av_seek_frame()` with `AVSEEK_FLAG_BACKWARD`
- `streamInfo()`: extract from `AVCodecParameters`
- `close()`: cleanup avformat

### 2.3 VideoDecoder (`src/core/`)
- `open(VideoStreamInfo)`: `avcodec_alloc_context3` + `avcodec_open2`
- `decode(VideoPacket) -> std::optional<DecodedFrame>`: send/receive API
- `flush() -> std::vector<DecodedFrame>`: drain buffered frames
- `reset()`: `avcodec_flush_buffers()`
- Handle the send/receive dance (EAGAIN, EOF)

### 2.4 PacketQueue (`src/core/`)
- Bounded SPSC (single-producer single-consumer) queue
- `push(VideoPacket)` blocks when full (backpressure)
- `pop() -> std::optional<VideoPacket>` blocks when empty
- `clear()` for flushing on seek
- `shutdown()` to unblock waiters
- Capacity: configurable, default ~64 packets

### 2.5 PlaybackController (`src/core/`)
- State machine: `STOPPED → PLAYING ↔ PAUSED`
- Owns source thread (reads packets → PacketQueue)
- Owns decode thread (PacketQueue → decode → FrameBuffer)
- UI thread timer: on tick, check if next frame PTS ≤ playback clock, if so push to QVideoSink
- Playback clock: `start_wall_time + (elapsed * speed)` mapped to video PTS
- Signals: `frameReady(QVideoFrame)`, `positionChanged(Timestamp)`, `stateChanged(PlaybackState)`

### 2.6 Minimal VideoWidget (`src/qt/`)
- QWidget containing a QVideoWidget
- Play/Pause button
- Current time label
- Connected to PlaybackController signals

### Verification
- Demo app plays an MP4 file forward
- Play/Pause works
- Video displays smoothly at correct frame rate
- Time label updates
- No memory leaks (valgrind/ASan)

---

## Phase 3: Seeking + Frame Stepping

**Goal**: Frame-accurate seeking, step forward/back, seek slider.

### 3.1 KeyframeIndex (`src/core/`)
- Built at file open by scanning `AVStream->index_entries` or iterating packets
- Sorted vector of `{timestamp, byte_offset}`
- `nearestKeyframeBefore(Timestamp) -> KeyframeEntry`
- `nearestKeyframeAfter(Timestamp) -> KeyframeEntry`
- `allKeyframes() -> span<KeyframeEntry>` for seek slider tick marks

### 3.2 Frame-accurate seek
- In PlaybackController: `seekTo(Timestamp target)`
  1. Pause source/decode threads
  2. Clear PacketQueue
  3. `source->seekTo(keyframeIndex.nearestKeyframeBefore(target))`
  4. `decoder->reset()` (flush buffers)
  5. Resume source thread → decode until frame PTS ≥ target
  6. Discard frames before target, display target frame
  7. Resume or stay paused depending on prior state

### 3.3 FrameBuffer (`src/core/`)
- Ring buffer of `DecodedFrame` around current position
- Configurable capacity (default: 256 MB budget → ~80 frames @ 1080p YUV420P)
- `push(DecodedFrame)` evicts oldest when over budget
- `frameAt(Timestamp) -> DecodedFrame*` for cache hits
- `framesBetween(Timestamp, Timestamp) -> span<DecodedFrame>` for GOP access
- Frames stored in decode order, indexed by PTS

### 3.4 Step forward / step backward
- `stepForward()`: decode next frame, push to display, stay paused
- `stepBackward()`:
  1. Find previous frame's PTS from FrameBuffer (if cached) → display it
  2. If not cached: seek to keyframe before (current - 1 frame), decode forward to (current - 1), display

### 3.5 VideoWidget updates
- Seek slider (QSlider) connected to `seekTo()`
- Step forward/back buttons
- Keyboard shortcuts: Space (play/pause), Left/Right (step), Shift+Left/Right (seek ±1s)

### Verification
- Click anywhere on seek slider → video jumps to correct frame
- Step forward shows exactly next frame
- Step backward shows exactly previous frame
- Seeking is fast (<200ms for typical MP4 with 1s GOPs)
- Frame buffer stays within memory budget

---

## Phase 4: Reverse Playback + Speed Control

**Goal**: Continuous reverse playback, variable speed (0.25x–4x forward and reverse).

### 4.1 GOP-based reverse decode
- PlaybackController enters `REVERSE` direction
- Algorithm:
  1. Find current GOP's keyframe via KeyframeIndex
  2. Seek to previous keyframe
  3. Decode entire GOP into FrameBuffer (forward decode)
  4. Present frames in reverse PTS order
  5. Double-buffer: start decoding next-reverse GOP on decode thread while presenting current

### 4.2 Speed control
- PlaybackController: `setSpeed(double)` — positive for forward, negative for reverse
- Playback clock: `presentation_time = start_pts + elapsed_wall_time * speed`
- Frame scheduler adjusts timer interval: `frame_duration / abs(speed)`
- Skip frames when speed > 2x to maintain timing (decode all, display subset)

### 4.3 VideoWidget updates
- Speed control widget (buttons or slider: 0.25x, 0.5x, 1x, 2x, 4x)
- Reverse button (or negative speed)
- Visual indicator for current speed/direction

### Verification
- Hold reverse: video plays backward smoothly at 1x speed
- Set speed to 2x forward: video plays at double speed, smooth
- Set speed to 0.5x: video plays at half speed, smooth
- Reverse at 2x: smooth, correct frame order
- Memory stays bounded during long reverse playback

---

## Phase 5: Timeline Synchronization Interface

**Goal**: Abstract bidirectional sync between video and an external timeline (PlotJuggler integration point).

### 5.1 TimelineBridge (`src/qt/`)
- Abstract interface for bidirectional time sync:

```cpp
class TimelineBridge : public QObject {
    Q_OBJECT
signals:
    // Video → external: video playback updated the current time
    void videoTimeChanged(Timestamp ts);
    // Video → external: video duration/range known
    void videoRangeChanged(Timestamp start, Timestamp end);

public slots:
    // External → video: external timeline wants to set the time
    void onExternalTimeChanged(Timestamp ts);
    // External → video: external timeline wants to play/pause
    void onExternalPlaybackCommand(PlaybackState state);
};
```

### 5.2 Demo app timeline
- Simple QSlider acting as a timeline
- Moving the slider calls `onExternalTimeChanged()` → video seeks
- Video playback emits `videoTimeChanged()` → slider moves
- Demonstrates the bidirectional sync without needing PlotJuggler

### 5.3 Lock modes
- `SYNC_BIDIRECTIONAL`: either can drive the other (default)
- `SYNC_VIDEO_MASTER`: only video drives the timeline
- `SYNC_EXTERNAL_MASTER`: only external timeline drives video

### Verification
- Drag demo slider → video seeks to that position
- Play video → demo slider moves in sync
- Pause video, drag slider, resume → video continues from new position
- No feedback loops (moving slider doesn't trigger seek that moves slider again)

---

## Phase 6: MCAP Video Source

**Goal**: Read and play video from MCAP files (Foxglove CompressedVideo format).

### 6.1 McapVideoSource (`src/sources/`)
- `open(path)`: `mcap::McapReader::open()`, read summary section, find video channels
- Detect video channels by schema name (`foxglove.CompressedVideo` or `sensor_msgs/CompressedImage`)
- `readPacket()`: iterate messages, parse CompressedVideo schema, extract Annex B payload
- `seekTo()`: use MCAP chunk index to find the right chunk, then scan for keyframe
- Build KeyframeIndex from MCAP message scan (check Annex B NAL types for IDR frames)

### 6.2 Annex B packet parsing
- Parse H.264 NAL units: detect SPS/PPS (keyframe indicators), slice types
- Parse H.265 NAL units: detect VPS/SPS/PPS, IRAP frames
- AV1: detect Sequence Header OBU + Key Frame OBU

### 6.3 Schema detection
- Support Foxglove's `CompressedVideo` schema (Protobuf or JSON serialization)
- Support `sensor_msgs/CompressedImage` (ROS CDR serialization) for per-frame JPEG/PNG
- Auto-detect serialization from MCAP schema encoding field

### 6.4 Conan deps this phase
- `mcap/2.1.1` (header-only)

### Verification
- Demo app opens an MCAP file containing H.264 CompressedVideo messages
- Video plays forward, seeking works, reverse playback works
- Timestamps match MCAP message timestamps

---

## Phase 7: SRT Live Streaming Source

**Goal**: Receive and display live H.264/H.265 video over SRT with minimal latency.

### 7.1 SrtVideoSource (`src/sources/`)
- `open(uri)`: parse `srt://host:port?mode=caller` or `srt://0.0.0.0:port?mode=listener`
- Configure SRT socket: `SRTO_LATENCY` (tunable, default 120ms), `SRTO_RCVBUF`
- `readPacket()`: `srt_recvmsg2()`, parse Annex B NAL units, produce VideoPackets
- `isSeekable()` → false, `isLive()` → true, `duration()` → nullopt

### 7.2 Low-latency decode path
- When source is live, PlaybackController switches to low-latency mode:
  - PacketQueue max size = 2 (minimal buffering)
  - No FrameBuffer caching (decode → display immediately)
  - Frame scheduler in "ASAP" mode (display each frame as soon as decoded)
  - Skip seeking controls in UI (grey out seek slider, step buttons)

### 7.3 Small rewind buffer for live
- Optional: keep last N seconds of decoded frames in FrameBuffer
- Allow seeking backward within the buffer (e.g., "rewind 5 seconds")
- Beyond buffer → not available

### 7.4 Connection management
- Auto-reconnect on disconnect (configurable interval)
- State signals: `connecting`, `connected`, `disconnected`
- UI indicator for connection status and latency

### 7.5 VideoWidget updates
- Connection status indicator (connected/disconnected/latency)
- When live: hide seek slider, show latency metric
- When live+buffered: show small rewind range

### 7.6 Conan deps this phase
- `srt/1.5.4`

### Verification
- Use FFmpeg to stream H.264 over SRT: `ffmpeg -re -i test.mp4 -c:v libx264 -f mpegts srt://127.0.0.1:9000`
- Demo app receives and displays with <200ms latency
- Disconnect/reconnect works
- No memory growth over extended streaming

---

## Phase 8: Hardware Acceleration

**Goal**: Cross-platform GPU-accelerated video decoding.

### 8.1 HW device auto-detection
- On `VideoDecoder::open()`, probe available HW devices in priority order:
  - Linux: VAAPI → CUDA → (software fallback)
  - Windows: D3D11VA → DXVA2 → (software fallback)
  - macOS: VideoToolbox → (software fallback)
- Use `av_hwdevice_ctx_create()` to initialize

### 8.2 HW frame handling
- When HW decode active, `AVFrame` contains GPU surface handles (not CPU data)
- For FrameBuffer storage: `av_hwframe_transfer_data()` to copy GPU → CPU (~0.5-2ms per 1080p)
- For direct display (no buffer needed): explore QRhiWidget path for zero-copy (optional optimization)

### 8.3 Fallback
- If HW init fails → graceful fallback to software decode
- Log which backend is active for debugging
- User can force software-only via config flag

### Verification
- On Linux with VAAPI: `vainfo` confirms VAAPI, decoder logs show HW accel active
- On macOS: VideoToolbox active in logs
- Benchmark: HW decode should be 3-5x faster than software for 4K
- Graceful fallback when HW not available

---

## Conan Dependencies Summary

| Phase | Package | Version | Purpose |
|-------|---------|---------|---------|
| 1–8 | `ffmpeg` | 8.0.1 | Decode/encode engine |
| 1–8 | Qt 6.8 | (system) | UI framework |
| 6 | `mcap` | 2.1.1 | MCAP file reading |
| 7 | `srt` | 1.5.4 | Live streaming |

Optional / later:
- `libdatachannel/0.24.0` — if WebRTC needed
- `dav1d/1.5.3` — standalone AV1 decoder (FFmpeg already includes it, but useful for MCAP direct decode)

---

## Key Design Decisions

1. **FFmpeg custom pipeline, not QMediaPlayer** — QMediaPlayer cannot do frame-accurate seeking, reverse playback, frame stepping, or low-latency streaming.

2. **QAbstractVideoBuffer bridge** (Qt 6.8 public API) — wrap AVFrame data planes into QVideoFrame for Qt rendering. Qt handles YUV→RGB via built-in shaders. Thread-safe via QVideoSink.

3. **VideoSource abstraction** — unified interface for file and stream sources. Capability flags (`isSeekable`, `isLive`) control which features are enabled. Downstream pipeline is identical.

4. **PacketQueue decouples I/O from decode** — source thread handles I/O (file read or network recv), decode thread processes packets. Bounded queue provides backpressure.

5. **FrameBuffer as ring buffer** — stores decoded frames around current position for seeking/scrubbing/reverse. Memory-budgeted, YUV420P storage (Qt handles conversion).

6. **TimelineBridge for PlotJuggler integration** — abstract signal/slot interface. Video emits time updates, external timeline can drive seeking. Later: implement PlotJuggler-specific bridge that connects to PJ's timeline API.

7. **No B-frames convention** — matching Foxglove and Rerun. Simplifies decode order, seeking, and reverse playback.

---

## Risk Areas

| Risk | Mitigation |
|------|------------|
| QAbstractVideoBuffer zero-copy may have hidden memcpy in Qt internals | Test early (Phase 1); fallback to QRhiWidget for manual GPU upload |
| FFmpeg 8.0 API changes from 7.x | Pin to 8.0.1 via Conan; wrap all FFmpeg calls in our own API |
| HW accel platform differences | Phase 8 is late; software works everywhere; HW is an optimization |
| MCAP schema parsing complexity | Start with Foxglove's well-documented schema; use MCAP library's schema registry |
| SRT latency tuning | Start with defaults (120ms); tune per use case; provide config knobs |
| Qt 6.8 availability on Conan | May need system Qt; document both paths |
| Reverse playback memory for 4K | Budget 744 MB for double-buffered 4K; make configurable; document limits |
