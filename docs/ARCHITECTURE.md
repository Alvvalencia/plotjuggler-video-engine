# VideoEngine — Architecture

## Overview

This document covers two layers:

1. **The VideoEngine library** (C++20, Qt 6.8, FFmpeg 6.1.1) — built as a standalone project.
2. **The VideoController adapter layer** — connects VideoEngine to PlotJuggler at integration time.

It is intended to complement `EVALUATION.md` — both documents should be read together.

> **Status:** Phases 1–3 implemented and tested (59/59 tests, ASan clean). Core playback pipeline, seeking, and frame stepping are operational. Next: Phase 4 (reverse playback + speed control).

---

## 1. Design Goals

| Goal | Rationale |
|---|---|
| Testability from day one | Phase 1 tests run without FFmpeg or any real video — only MockVideoBackend |
| Standalone first | VideoEngine is an independent C++20/Qt 6.8 library; PlotJuggler integration is the final step |
| Backend independence | IVideoBackend facade hides the full FFmpeg pipeline; VideoController never touches internals |
| Incremental development | Each evaluation phase unlocks independently; the architecture supports partial builds |
| Thread safety | VideoEngine runs 3 threads internally; the IVideoBackend public API must be safe to call from Qt main thread |
| PlotJuggler integration (deferred) | Final step: TimelineBridge + StatePublisher adapter; not a constraint on current design |

---

## 2. Core Principle — Dependency Inversion

The central architectural decision is that **VideoController never calls any video library directly**. It only calls an abstract interface (`IVideoBackend`). The concrete implementation — whether real or fake — is injected at construction time.

This means:

- The logic layer (`VideoController`) is fully testable without video files.
- Swapping the video backend requires zero changes to the controller.
- The mock backend used in tests and the real backend used in production are interchangeable.

In the standalone demo app, `FFmpegBackend` is injected directly. In PlotJuggler, `VideoController` becomes a `StatePublisher` adapter that wraps `FFmpegBackend`.

---

## 3. Component Overview

```
Demo App / PlotJuggler (future integration via TimelineBridge)
     │
     │ Qt signals / StatePublisher::updateState()
     ▼
VideoController          ← adapter: control logic + PlotJuggler seam
     │
     │ IVideoBackend* (public API boundary — the only coupling point)
     ├──────────────────────────────────────────┐
     ▼                                          ▼
MockVideoBackend                          FFmpegBackend        (Phase 2+)
(tests only, Phase 1)                     implements IVideoBackend
                                                │
                                                ▼  wraps  [VideoEngine internals]
                                         PlaybackController
                                              │
                                   ┌──────────┼──────────────────┐
                                   ▼          ▼                  ▼
                               VideoSource  PacketQueue     FrameBuffer
                               (abstract)   (SPSC queue)   (ring + LRU)
                                   │
                          ┌────────┴───────────┐
                          ▼                    ▼
                    FileVideoSource      McapVideoSource     SrtVideoSource
                    (Phase 2)            (Phase 6)           (Phase 7)
                                          │
                                     VideoDecoder (FFmpeg avcodec)
                                          │
                                     QVideoSink (Qt 6.8)
```

---

## 4. Component Descriptions

### 4.1 `IVideoBackend` — Abstract Interface

**Role:** Defines the contract that every backend must fulfill. `VideoController` depends only on this interface — never on a concrete class.

**Type:** Pure abstract C++ class (all methods = 0). Must declare `virtual ~IVideoBackend() = default;` for safe `std::unique_ptr` ownership.

**Declared methods:**

| Method | Signature | Notes |
|---|---|---|
| open | `virtual bool open(const QString& path) = 0` | — |
| play | `virtual void play() = 0` | — |
| pause | `virtual void pause() = 0` | — |
| stop | `virtual void stop() = 0` | — |
| seek | `virtual bool seek(int64_t us) = 0` | Unit: microseconds |
| stepForward | `virtual bool stepForward() = 0` | Advance by one frame (Phase 3) |
| stepBackward | `virtual bool stepBackward() = 0` | Go back by one frame (Phase 3) |
| connectToSink | `virtual void connectToSink(QVideoSink* sink) = 0` | Single frame delivery model — see Section 4.5 |
| getState | `virtual PlaybackState getState() const = 0` | — |
| getDurationUs | `virtual int64_t getDurationUs() const = 0` | — |
| getPositionUs | `virtual int64_t getPositionUs() const = 0` | — |

**Frame delivery:** All backends use the same push model via `connectToSink()`. See Section 4.5.

**PlaybackState note:** The enum `PlaybackState` uses a 6-state model (IDLE/LOADED/PLAYING/PAUSED/STOPPED/ERROR), intentionally richer than the engine-level 3-state model (STOPPED/PLAYING/PAUSED); the extra states exist at the adapter layer.

```cpp
enum class PlaybackState {
    IDLE,       // No video loaded
    LOADED,     // Video loaded, not playing
    PLAYING,    // Actively playing
    PAUSED,     // Paused mid-playback
    STOPPED,    // Stopped, position reset
    ERROR       // Unrecoverable error
};
```

---

### 4.2 `VideoController` — Adapter + Control Logic

**Role:** Serves two roles:

1. **Testable adapter** — wraps IVideoBackend, enforces state machine, validates inputs. Used with MockVideoBackend in Phase 1 tests.
2. **PlotJuggler integration adapter** — at integration time, becomes the implementation body of PlotJuggler's `StatePublisher::updateState()` callback. Connects to VideoEngine via IVideoBackend.

**Receives:** An `IVideoBackend` instance via constructor injection (`std::unique_ptr<IVideoBackend>`).

**State ownership:** VideoController is the **sole gatekeeper** of the state machine. It validates every transition *before* forwarding to the backend. The backend executes the work but does not enforce transitions. If `controller.play()` is called from IDLE, VideoController rejects it without calling `backend_->play()`.

**Public API:**

```cpp
class VideoController : public QObject {
    Q_OBJECT
public:
    explicit VideoController(std::unique_ptr<IVideoBackend> backend);
    ~VideoController();

    // Playback control — return false if transition is invalid
    bool open(const QString& path);
    bool play();
    bool pause();
    bool stop();
    bool seek(int64_t us);
    bool stepForward();     // Phase 3: advance one frame, transition to PAUSED
    bool stepBackward();    // Phase 3: go back one frame, transition to PAUSED

    // Frame delivery — forwarded to backend
    void connectToSink(QVideoSink* sink);

    // State queries
    PlaybackState getState() const;
    int64_t getDurationUs() const;
    int64_t getPositionUs() const;

signals:
    void stateChanged(PlaybackState newState);
    void positionChanged(int64_t us);
    void errorOccurred(const QString& message);

private:
    std::unique_ptr<IVideoBackend> backend_;
    PlaybackState state_ = PlaybackState::IDLE;
};
```

**Usage:**

```cpp
// In production (standalone demo)
auto ctrl = new VideoController(std::make_unique<FFmpegBackend>());

// In tests (EVALUATION Phase 1)
auto ctrl = new VideoController(std::make_unique<MockVideoBackend>());
```

---

### 4.3 `MockVideoBackend` — Test Implementation

**Role:** Fake implementation of `IVideoBackend` used exclusively in unit tests (EVALUATION Phase 1). Simulates all states, transitions, and error conditions without any real video dependency.

**Lives in:** Test codebase only — never shipped in production.

**Capabilities:**
- Configurable responses per method (e.g. force `open()` to return false)
- Call counters (verify that `seek()` was called exactly once)
- Simulated state machine matching the real backend's behavior
- Configurable duration and position values
- Implements `connectToSink()` — can push synthetic `QVideoFrame` instances to the connected sink for frame delivery tests

**Does NOT:**
- Decode any video
- Read any file from disk
- Depend on FFmpeg

**Note:** MockVideoBackend depends on Qt (Core, Gui, Multimedia) for `QVideoFrame`, `QVideoSink`, `QString`, and `QImage` types used in the `IVideoBackend` interface. It does not depend on FFmpeg or any real video codec.

---

### 4.4 `FFmpegBackend` — Production Implementation

**Role:** Production implementation of `IVideoBackend`. Wraps the VideoEngine internal pipeline, exposing it through the `IVideoBackend` facade. `VideoController` never touches any of the internal components directly.

**Technology:** FFmpeg 6.1.1 (system, via pkg-config), Qt 6.8.

**Internal pipeline (VideoEngine architecture):**

```
FFmpegBackend (implements IVideoBackend)
  └── PlaybackController  ← threads + PTS scheduler
        ├── VideoSource   ← I/O abstraction
        │     ├── FileVideoSource   ← local file (MP4, MKV...)
        │     ├── McapVideoSource   ← MCAP container (Phase 6)
        │     └── SrtVideoSource    ← live SRT stream (Phase 7)
        ├── PacketQueue   ← SPSC bounded queue
        ├── VideoDecoder  ← FFmpeg avcodec wrapper
        └── FrameBuffer   ← ring buffer + LRU
```

**Threading note:** All IVideoBackend methods (play/pause/stop/seek/stepForward/stepBackward/open) are called from the Qt main thread by VideoController. FFmpegBackend internally manages worker threads (source, decode). The IVideoBackend API must therefore be thread-safe on entry — seeking must pause worker threads, flush the queue, reposition, and resume.

> **Implemented in Phases 2–3.** Playback, seeking, and frame stepping are operational.

---

### 4.5 `VideoSource` — I/O Abstraction (Phase 2+)

**Role:** Unified interface for all input sources. Capability flags distinguish file-based from live sources — everything downstream (decode, buffer, render) is shared.

```cpp
class VideoSource {
public:
    virtual ~VideoSource() = default;

    virtual bool open(const std::string& uri) = 0;
    virtual void close() = 0;
    virtual std::optional<VideoPacket> readPacket() = 0;

    // Capability flags
    virtual bool isSeekable() const = 0;
    virtual bool isLive() const = 0;

    // File-only (return nullopt/false for live)
    virtual std::optional<Duration> duration() const = 0;
    virtual bool seekTo(Timestamp ts) = 0;
    virtual const KeyframeIndex& keyframeIndex() const = 0;

    virtual VideoStreamInfo streamInfo() const = 0;
};
```

**Note on `std::string` vs `QString`:** VideoSource uses `std::string` internally — it's FFmpeg-level code with no Qt dependency. The conversion `QString → std::string` happens once inside `FFmpegBackend::open()`.

| Source | isSeekable | isLive | duration | seekTo | Phase |
|--------|-----------|--------|----------|--------|-------|
| FileVideoSource | true | false | file length | seeks in file | 2 |
| McapVideoSource | true | false | time range | jumps via MCAP index | 6 |
| SrtVideoSource | false | true | nullopt | no-op | 7 |

---

### 4.6 Frame Delivery Model

A single push model is used by all backends — both mock and production.

**How it works:**
- The consumer calls `connectToSink(QVideoSink*)` to register a sink on the backend
- The backend pushes frames by calling `QVideoSink::setVideoFrame(QVideoFrame)` — thread-safe in Qt 6.8
- Qt handles YUV→RGB via built-in GPU shaders
- No frame pacing built into QVideoSink — the backend must implement its own PTS-based scheduler

**Per backend:**
- **MockVideoBackend:** stores the sink pointer. Can push synthetic `QVideoFrame` instances (e.g., solid-color frames at a given resolution) for frame delivery tests. Phase 1 unit tests that only verify state logic do not need to call `connectToSink()`.
- **FFmpegBackend:** UI-thread timer fires when next frame PTS is due. Pushes decoded video frames wrapped in `FFmpegVideoBuffer` (QAbstractVideoBuffer subclass).

**Display path:**
- Demo app: Backend → QVideoSink → QVideoWidget (zero-copy path)
- PlotJuggler integration: same push model; PlotJuggler's render widget connects to QVideoSink

**Why a single model:** Having all backends implement the same delivery contract means the interface is honest — every method is meaningful for every implementation. Tests that need frame delivery use the same code path as production.

---

### 4.7 Threading Model

VideoEngine internally runs three threads. VideoController always operates on the Qt main thread.

```
Source Thread ──► PacketQueue ──► Decode Thread ──► FrameBuffer ──► UI Thread (scheduler + render)
     │                                                                    │
     (I/O bound)                                              (timer: QVideoSink::setVideoFrame())
                                                                          │
                                                               ◄──────────┘ VideoController::seek()
                                                                 (called from here; must be safe)
```

**Rules:**
- VideoController is always called from the **Qt main thread** (QObject thread affinity)
- IVideoBackend methods must be **re-entrant from main thread only** (no concurrent calls from multiple threads)
- FFmpegBackend internally dispatches to source/decode threads
- `QVideoSink::setVideoFrame()` is Qt 6.8 documented thread-safe — safe to call from timer thread

---

## 5. State Machine

**Design principle:** Redundant operations are **no-ops**, not errors. PlotJuggler's timeSlider will send seek/play/pause commands without tracking the video state — the controller must tolerate this gracefully.

```
  Any state ──── open(valid) ──► LOADED   (close current first if needed)
  Any state ──── open(invalid) ──► [unchanged]  (return false, emit errorOccurred)

  LOADED ─── play() ──► PLAYING
  LOADED ─── stop() ──► STOPPED
  LOADED ─── seek() ──► LOADED (repositions, stays LOADED)
  LOADED ─── stepForward() ──► PAUSED (advance one frame)
  LOADED ─── stepBackward() ──► PAUSED (go back one frame)

  PLAYING ─── pause() ──► PAUSED
  PLAYING ─── stop() ──► STOPPED
  PLAYING ─── seek() ──► PLAYING (continues from new position)
  PLAYING ─── play() ──► PLAYING [no-op]
  PLAYING ─── stepForward() ──► [rejected, returns false]
  PLAYING ─── stepBackward() ──► [rejected, returns false]

  PAUSED ─── play() ──► PLAYING
  PAUSED ─── stop() ──► STOPPED
  PAUSED ─── seek() ──► PAUSED (repositions, stays PAUSED)
  PAUSED ─── pause() ──► PAUSED [no-op]
  PAUSED ─── stepForward() ──► PAUSED (advance one frame)
  PAUSED ─── stepBackward() ──► PAUSED (go back one frame)

  STOPPED ─── play() ──► PLAYING (from position 0)
  STOPPED ─── seek() ──► STOPPED (repositions)
  STOPPED ─── stop() ──► STOPPED [no-op]
  STOPPED ─── stepForward() ──► PAUSED (advance one frame)
  STOPPED ─── stepBackward() ──► PAUSED (go back one frame)

  ERROR ─── open(valid) ──► LOADED [recovery path]
```

**Rejected from IDLE (no video loaded):** `play()`, `pause()`, `stop()`, `seek()` — all return false.

**`open()` failure:** Returns false, state stays unchanged. `errorOccurred` signal emitted with reason. ERROR state is reserved for unrecoverable runtime failures (decoder crash, stream corruption), not for validation errors like "file not found" or empty path.

**Seek rules:**
- `seek(us < 0)` → return false, position unchanged
- `seek(us > duration)` → clamp to `duration - one_frame`, return true
- `seek()` is valid in LOADED, PLAYING, PAUSED, and STOPPED

**No-ops return true** (the operation is idempotent, not an error).

**Error handling — no exceptions in the public API.** All errors are reported via `bool` return values and the `errorOccurred` signal. The API never throws.

**Notes:**
- `REVERSE` is a **direction flag** inside PlaybackController, not a state. Direction is only meaningful in PLAYING state.
- `open()` from any non-IDLE state closes the current video first, then opens the new one. There is no separate `close()` method — `open()` handles cleanup internally before reopening.

### 5.1 Adapter ↔ Engine State Mapping

VideoController (adapter) uses 6 states. PlaybackController (engine) uses 3. The mapping:

| VideoController | PlaybackController | Notes |
|---|---|---|
| IDLE | (not created) | No backend pipeline active |
| LOADED | STOPPED | File opened, position at start |
| PLAYING | PLAYING | — |
| PAUSED | PAUSED | — |
| STOPPED | STOPPED | Position reset to 0 |
| ERROR | (not created) | Pipeline torn down |

**Thread safety:** State lives in VideoController (`state_` member, Qt main thread only). FFmpegBackend methods are called sequentially from the main thread — no mutex needed at the IVideoBackend boundary.

---

## 6. File Structure

The standalone project structure, combining the VideoEngine internals (the pipeline design) and the adapter layer:

```
VideoEngine/
├── CMakeLists.txt                       ← C++20, Qt 6.8, FFmpeg (pkg-config), GTest (Conan)
├── conanfile.py                         ← GTest only; FFmpeg via system pkg-config
├── docs/
│   ├── ARCHITECTURE.md                  ← this document
│   ├── EVALUATION.md                    ← evaluation plan
│   ├── PLAN.md                          ← execution plan
│   ├── RESEARCH.md                      ← technology research
│   └── USAGE.md                         ← build, run, and test instructions
│
├── include/VideoEngine/                 ← public API headers
│   ├── IVideoBackend.h                  ← pure virtual interface (Phase 1, updated Phase 3)
│   ├── VideoController.h                ← adapter + state machine (Phase 1, updated Phase 3)
│   └── PlaybackState.h                  ← enum: IDLE/LOADED/PLAYING/PAUSED/STOPPED/ERROR
│
├── src/
│   ├── VideoController.cpp              ← state machine enforcement + delegation
│   ├── FFmpegBackend.h/.cpp             ← IVideoBackend production implementation (Phase 2–3)
│   ├── core/
│   │   ├── types.h                      ← Timestamp, Duration, Codec enum
│   │   ├── video_packet.h               ← RAII AVPacket* wrapper
│   │   ├── decoded_frame.h/.cpp         ← RAII AVFrame* wrapper (refcounted via av_frame_ref)
│   │   ├── video_source.h               ← abstract VideoSource interface
│   │   ├── video_decoder.h/.cpp         ← FFmpeg avcodec send/receive wrapper
│   │   ├── packet_queue.h/.cpp          ← bounded thread-safe SPSC queue
│   │   ├── frame_buffer.h/.cpp          ← ring buffer with memory budget + PTS lookup (Phase 3)
│   │   ├── keyframe_index.h/.cpp        ← keyframe PTS scanner + binary search (Phase 3)
│   │   └── playback_controller.h/.cpp   ← 3-thread pipeline + seeking + stepping
│   ├── sources/
│   │   └── file_video_source.h/.cpp     ← local MP4/MKV via avformat
│   └── qt/
│       ├── ffmpeg_video_buffer.h/.cpp   ← QAbstractVideoBuffer for AVFrame display
│       └── video_widget.h/.cpp          ← QWidget with controls + keyboard shortcuts (Phase 3)
│
├── demo/
│   └── main.cpp                         ← standalone demo app
│
└── tests/
    ├── generate_test_data.sh            ← generates test_coarse.mp4
    ├── data/test_coarse.mp4             ← 30s 1080p H.264 test video (gitignored)
    ├── test_main.cpp                    ← custom GTest main with QCoreApplication
    ├── MockVideoBackend.h/.cpp          ← fake backend for unit tests
    ├── test_video_selection.cpp         ← 4 tests (Phase 1)
    ├── test_state_transitions.cpp       ← 7 tests (Phase 1)
    ├── test_seek.cpp                    ← 6 tests (Phase 1)
    ├── test_robustness.cpp              ← 4 tests (Phase 1)
    ├── test_video_decoder.cpp           ← 8 tests (Phase 2)
    ├── test_packet_queue.cpp            ← 6 tests (Phase 2)
    ├── test_ffmpeg_backend.cpp          ← 4 tests (Phase 2)
    ├── test_keyframe_index.cpp          ← 6 tests (Phase 3)
    ├── test_frame_buffer.cpp            ← 6 tests (Phase 3)
    └── test_seeking.cpp                 ← 8 tests (Phase 3)
```

---

## 7. Build System

**Language standard:** C++20.

**Framework:** CMake 3.20+.

**Qt version:** Qt 6.8.

**CMake structure:**

```cmake
cmake_minimum_required(VERSION 3.20)
project(VideoEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Multimedia Widgets MultimediaWidgets)
find_package(GTest REQUIRED)            # via Conan
find_package(PkgConfig REQUIRED)

# FFmpeg via system pkg-config (not Conan — avoids xorg/pulseaudio transitive deps)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC  REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL   REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE  REQUIRED IMPORTED_TARGET libswscale)

# find_package(mcap REQUIRED)           # Phase 6: mcap/2.1.1
# find_package(srt REQUIRED)            # Phase 7: srt/1.5.4
```

**Dependencies:** GTest via `conanfile.py` (Python format). FFmpeg via system pkg-config. Qt 6.8 via system install (`CMAKE_PREFIX_PATH`).

---

## 8. FPS Reference & Temporal Parameters

These values are agreed and used across both `ARCHITECTURE.md` and `EVALUATION.md`.

| Parameter | Value | Notes |
|---|---|---|
| Reference FPS | 30 FPS | Agreed |
| Frame duration | 33,333 µs | 1,000,000 / 30 |
| Fine tolerance (N=1) | 33,333 µs | EVALUATION Phase 3 |
| Timestamp type | `Timestamp` (`int64_t µs`) | from types.h |
| Seek unit | **microseconds** | Current standard |

---

## 9. Resolved Decisions

| # | Decision | Resolution |
|---|---|---|
| 1 | Video decoding library | FFmpeg 6.1.1 via system pkg-config (Conan ffmpeg had impractical transitive deps) |
| 2 | Frame delivery model | Push via `connectToSink(QVideoSink*)` — single model for all backends |
| 3 | Project directory | Standalone `VideoEngine/` project (see Section 6) |
| 4 | Qt version | Qt 6.8 (standalone); Qt5↔Qt6 boundary resolved at PlotJuggler integration |
| 5 | GTest integration | Via Conan (`gtest`) for reproducibility |
| 6 | Test video format | MP4, H.264, CFR 30fps, 1080p, 30s (Phase 2); MCAP + MKV in Phases 6+ |
| 7 | C++ standard | C++20 throughout |
| 8 | Timestamp unit | Microseconds (`int64_t`); adopt `Timestamp` alias from types.h in Phase 2 |
| 9 | Method naming | camelCase throughout |
| 10 | conanfile format | Python (conanfile.py) per PLAN.md |

---

## 10. Component Mapping (PLAN.md)

Cross-reference between the `IVideoBackend` abstraction layer and the component names used in PLAN.md.

| This document | PLAN.md | Notes |
|---|---|---|
| `IVideoBackend` | (facade interface) | The interface that hides the pipeline |
| `FFmpegBackend` | `FFmpegBackend` | Same name — implements IVideoBackend |
| (internal) | `PlaybackController` | Thread management + PTS scheduler; owned by FFmpegBackend |
| (internal) | `VideoSource` | I/O abstraction inside PlaybackController |
| (internal) | `FileVideoSource` | Local file I/O (MP4, MKV) |
| (internal) | `McapVideoSource` | MCAP container support (Phase 6) |
| (internal) | `SrtVideoSource` | SRT live stream support (Phase 7) |
| (internal) | `PacketQueue` | SPSC bounded queue between source and decoder |
| (internal) | `VideoDecoder` | FFmpeg `avcodec` wrapper |
| (internal) | `FrameBuffer` | Ring buffer + LRU cache for decoded frames |
| `VideoController` | `VideoController` | Same role — sits above IVideoBackend, not inside the pipeline |
| `MockVideoBackend` | (test only) | No equivalent in production plan |
| `TimelineBridge` | `timeline_bridge.h/.cpp` | Bidirectional PlotJuggler sync (Phase 5 of PLAN.md) — signals: `videoTimeChanged(Timestamp)`, `onExternalTimeChanged(Timestamp)`, `onExternalPlaybackCommand(PlaybackState)` |

> **Key insight:** VideoController in both models sits above the IVideoBackend boundary. The entire PlaybackController pipeline is an implementation detail of FFmpegBackend — it is never visible to VideoController.

---

## 11. Future: PlotJuggler Integration

This section describes how VideoEngine integrates into PlotJuggler as a `StatePublisher` plugin. This is **Phase 5 of PLAN.md** and is deferred until Phase 2 is complete and the PlotJuggler Qt version is confirmed.

**VideoController as StatePublisher adapter:**
- Inherits `PJ::StatePublisher`
- `updateState(double time_s)` → `controller_->seek(static_cast<int64_t>(time_s * 1e6))`
- `play(double interval)` → forward to VideoController
- `setEnabled(bool)` → start/stop VideoController

**TimelineBridge** replaces direct StatePublisher calls for bidirectional sync:
- Video emits `videoTimeChanged(Timestamp)` → PlotJuggler timeline scrubs
- PlotJuggler timeline emits → `onExternalTimeChanged(Timestamp)` → seek
- PlotJuggler emits → `onExternalPlaybackCommand(PlaybackState)` → play/pause

**Build requirements at integration:**
- `plotjuggler_base` CMake target
- `QT_PLUGIN` define
- Install to `${PJ_PLUGIN_INSTALL_DIRECTORY}`
- `Q_PLUGIN_METADATA` macro in plugin header

**Session persistence:**
- `xmlSaveState()` / `xmlLoadState()` for last-opened video path

**Qt version at integration time:** TBD — PlotJuggler may be Qt6 by then, or process isolation may be needed.

---

