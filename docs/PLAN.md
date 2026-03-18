# PlotJuggler Video Engine — Execution Plan

## Context

PlotJuggler needs video support: file playback (forward/reverse, frame-accurate seeking) and live streaming (low-latency, robot-to-desktop). The architecture must be unified from the start — both file and stream sources produce timestamped compressed packets, and everything downstream (decode, buffer, schedule, render) is shared. Built as a standalone C++20 library + demo app, later integrated into PlotJuggler.

See `RESEARCH.md` for full technology research.

---

> See `ARCHITECTURE.md` for all interface definitions, component design, state machine, file structure, and build system.

---

## Phase 1: Adapter Layer + Project Scaffold `COMPLETE`

**Goal**: CMake+Conan project with the `IVideoBackend` interface, `VideoController`, `MockVideoBackend`, and 21 unit tests. No FFmpeg — all tests run against the mock. See `EVALUATION.md` Phase 1 for the full test matrix.

### 1.1 Project setup
- `conanfile.py` with dependency: `gtest`
- Qt 6.8 via system install (aqtinstall) — found via `CMAKE_PREFIX_PATH`, not Conan
- `CMakeLists.txt` with C++20, Qt6 `find_package`, GTest `find_package`
- Cross-platform build verified (Linux, macOS, Windows)

### 1.2 Public API (`include/VideoEngine/`)
- `PlaybackState.h`: enum class (IDLE, LOADED, PLAYING, PAUSED, STOPPED, ERROR)
- `IVideoBackend.h`: pure abstract interface — `open`, `play`, `pause`, `stop`, `seek`, `connectToSink`, `getState`, `getDurationUs`, `getPositionUs`

### 1.3 VideoController (`src/`)
- Constructor injection: `std::unique_ptr<IVideoBackend>`
- State machine enforcement (valid and invalid transitions)
- Input validation (reject negative seek, guard calls from IDLE state)
- Qt signals on state changes

### 1.4 MockVideoBackend (`tests/`)
- Fake `IVideoBackend` implementation — no FFmpeg, no real video files
- Configurable responses, call counters, simulated state machine
- Implements `connectToSink()` — can push synthetic `QVideoFrame` to connected sink

### 1.5 Unit tests (`tests/`)
- `test_video_selection.cpp` — open valid/invalid/empty paths, replace loaded video
- `test_state_transitions.cpp` — play/pause/stop from all states, invalid transitions
- `test_seek.cpp` — valid/invalid seek values, seek during play/pause
- `test_robustness.cpp` — illegal call order, destructor safety, rapid seek

### Verification
- `cmake --build` succeeds on all platforms
- All 21 unit tests pass
- 0 crashes, 0 memory leaks (ASan)

**Result:** 21/21 tests passing, ASan clean. Completed 2026-03-18.

### Conan deps this phase
- Qt 6.8 (system install via aqtinstall)
- `gtest` (via Conan)

---

## Phase 2: Core Types + File Playback Pipeline `COMPLETE`

**Goal**: FFmpegBackend implements `IVideoBackend`. Play an MP4 file forward with play/pause. Threaded pipeline with packet queue. Smoke test: decode and display a single frame.

### 2.1 Core data types (`src/core/`)
- `types.h`: `Timestamp` (int64_t microseconds), `Duration`, `Codec` enum (H264, H265, AV1)
- `video_packet.h`: `VideoPacket{timestamp, data (std::vector<uint8_t>), is_keyframe, codec, stream_index}`
- `decoded_frame.h`: RAII wrapper around `AVFrame*` with ref-counting (`av_frame_ref`/`av_frame_unref`). Exposes width, height, pixel format, timestamp, plane data pointers.

### 2.2 FFmpegVideoBuffer (`src/qt/`)
- Subclass `QAbstractVideoBuffer`
- `format()`: map `AVPixelFormat` → `QVideoFrameFormat::PixelFormat`
- `map()`: return `AVFrame->data[]` pointers and `linesize[]` as `MapData`
- `unmap()`: no-op (frame owns the data)
- Constructor takes `DecodedFrame` by value (shared ref-counted AVFrame)

### 2.3 VideoSource interface (`src/core/video_source.h`)
- Define the abstract interface (as shown in Architecture above)
- `VideoStreamInfo`: codec, width, height, pixel_format, fps, time_base

### 2.4 FileVideoSource (`src/sources/`)
- `open(path)`: `avformat_open_input` + `avformat_find_stream_info` + find best video stream
- `readPacket()`: `av_read_frame()`, wrap in `VideoPacket`
- `seekTo()`: `av_seek_frame()` with `AVSEEK_FLAG_BACKWARD`
- `streamInfo()`: extract from `AVCodecParameters`
- `close()`: cleanup avformat

### 2.5 VideoDecoder (`src/core/`)
- `open(VideoStreamInfo)`: `avcodec_alloc_context3` + `avcodec_open2`
- `decode(VideoPacket) -> std::optional<DecodedFrame>`: send/receive API
- `flush() -> std::vector<DecodedFrame>`: drain buffered frames
- `reset()`: `avcodec_flush_buffers()`
- Handle the send/receive dance (EAGAIN, EOF)

### 2.6 PacketQueue (`src/core/`)
- Bounded SPSC (single-producer single-consumer) queue
- `push(VideoPacket)` blocks when full (backpressure)
- `pop() -> std::optional<VideoPacket>` blocks when empty
- `clear()` for flushing on seek
- `shutdown()` to unblock waiters
- Capacity: configurable, default ~64 packets

### 2.7 PlaybackController (`src/core/`)
- State machine: `STOPPED → PLAYING ↔ PAUSED`
- Owns source thread (reads packets → PacketQueue)
- Owns decode thread (PacketQueue → decode → FrameBuffer)
- UI thread timer: on tick, check if next frame PTS ≤ playback clock, if so push to QVideoSink
- Playback clock: `start_wall_time + (elapsed * speed)` mapped to video PTS
- Signals: `frameReady(QVideoFrame)`, `positionChanged(Timestamp)`, `stateChanged(PlaybackState)`

### 2.8 FFmpegBackend (`src/`)
- Implements `IVideoBackend` — wraps PlaybackController + pipeline components
- `connectToSink()`: registers the QVideoSink for frame delivery
- All IVideoBackend methods called from Qt main thread; FFmpegBackend dispatches to internal threads

### 2.9 Minimal VideoWidget (`src/qt/`)
- QWidget containing a QVideoWidget
- Play/Pause button
- Current time label
- Connected to PlaybackController signals

### 2.10 Smoke test + demo
- `demo/main.cpp`: open an MP4, decode and display video via FFmpegBackend → QVideoSink → QVideoWidget

### Verification
- Demo app plays an MP4 file forward
- Play/Pause works
- Video displays smoothly at correct frame rate
- Time label updates
- No memory leaks (valgrind/ASan)

**Result:** 39/39 tests passing (21 Phase 1 + 18 Phase 2), ASan clean. Completed 2026-03-18.

**Implementation note:** FFmpeg is provided via system pkg-config (libavformat 6.1.1, libavcodec, libavutil, libswscale) instead of Conan. The Conan `ffmpeg` package pulled heavy transitive dependencies (xorg, pulseaudio) that were impractical. The FFmpeg APIs used (send/receive decode, avformat) are stable across 6.x–8.x.

### Conan deps this phase
- Qt 6.8 (system install via aqtinstall)
- ~~`ffmpeg/8.0.1`~~ → system FFmpeg 6.1.1 via pkg-config (see note above)

---

## Phase 3: Seeking + Frame Stepping `COMPLETE`

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

**Result:** 59/59 tests passing (21 Phase 1 + 18 Phase 2 + 20 Phase 3), ASan clean. Completed 2026-03-18.

**Phase 3 additions to IVideoBackend:** `stepForward()` and `stepBackward()` virtual methods added. `VideoController` enforces these are only valid from PAUSED/LOADED/STOPPED states and transitions to PAUSED after stepping.

---

## Phase 4: Reverse Playback + Speed Control `NEXT`

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

## Dependencies Summary

| Phase | Package | Version | Source | Purpose |
|-------|---------|---------|--------|---------|
| 1–8 | Qt 6.8 | 6.8.3 | system (aqtinstall) | UI framework |
| 1–8 | GTest | latest | Conan | Unit / integration tests |
| 2–8 | FFmpeg | 6.1.1 | system (pkg-config) | Decode/encode engine |
| 6 | mcap | 2.1.1 | Conan (planned) | MCAP file reading |
| 7 | srt | 1.5.4 | Conan (planned) | Live streaming |

**Why FFmpeg via system instead of Conan:** The Conan `ffmpeg` package pulls heavy transitive deps (xorg, pulseaudio, ~20 X11/XCB dev packages). System FFmpeg 6.1.1 provides the same APIs we use (avformat, avcodec, avutil, swscale) with zero dependency friction.

Optional / later:
- `libdatachannel/0.24.0` — if WebRTC needed
- `dav1d/1.5.3` — standalone AV1 decoder (FFmpeg already includes it, but useful for MCAP direct decode)

---

## Risk Areas

| Risk | Status | Mitigation / Resolution |
|------|--------|------------------------|
| QAbstractVideoBuffer zero-copy may have hidden memcpy in Qt internals | **Resolved** | Tested in Phase 2 — `FFmpegVideoBuffer::map()` works correctly with AVFrame planes |
| FFmpeg version/API differences | **Resolved** | Using system FFmpeg 6.1.1 via pkg-config. APIs (send/receive, avformat) are stable across 6.x–8.x |
| Conan FFmpeg transitive deps | **Resolved** | Conan `ffmpeg` pulled xorg/pulseaudio (~20 dev packages). Switched to system pkg-config |
| HW accel platform differences | Open | Phase 8 is late; software works everywhere; HW is an optimization |
| MCAP schema parsing complexity | Open | Start with Foxglove's well-documented schema; use MCAP library's schema registry |
| SRT latency tuning | Open | Start with defaults (120ms); tune per use case; provide config knobs |
| Qt 6.8 availability on Conan | **Resolved** | Using system Qt via `CMAKE_PREFIX_PATH` |
| Reverse playback memory for 4K | Open | Budget 744 MB for double-buffered 4K; make configurable; document limits |
