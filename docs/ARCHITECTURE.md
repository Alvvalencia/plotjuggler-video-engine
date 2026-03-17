# VideoEngine — Architecture

## Overview

This document covers two layers:

1. **The VideoEngine library** (C++20, Qt 6.8, FFmpeg 8.0.1) — built as a standalone project. Components are fully designed but not yet coded (Phases 2+).
2. **The VideoController adapter layer** — connects VideoEngine to PlotJuggler at integration time. Currently only this layer (Phase 1) is implemented.

It is intended to complement `EV_PLAN.md` — both documents should be read together.

> **Status:** Phase 1 adapter layer complete. VideoEngine core (Phases 2+) pending.

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

**Type:** Pure abstract C++ class (all methods = 0).

**Declared methods:**

| Method | Signature | Notes |
|---|---|---|
| open | `virtual bool open(const QString& path) = 0` | — |
| play | `virtual void play() = 0` | — |
| pause | `virtual void pause() = 0` | — |
| stop | `virtual void stop() = 0` | — |
| seek | `virtual bool seek(int64_t us) = 0` | Unit: microseconds |
| getFrame | `virtual QImage getFrame() = 0` | Pull model for tests only |
| connectToSink | `virtual void connectToSink(QVideoSink*) = 0` | Push model for production (Phase 2) |
| getState | `virtual PlaybackState getState() const = 0` | — |
| getDurationUs | `virtual int64_t getDurationUs() const = 0` | — |
| getPositionUs | `virtual int64_t getPositionUs() const = 0` | — |

**Frame delivery note:** See Section 4.5 for the full explanation of pull (getFrame/MockVideoBackend) vs push (connectToSink/FFmpegBackend) delivery paths.

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

**Responsibilities:**
- State machine management (valid and invalid transitions)
- Input validation before forwarding to the backend (e.g. reject negative seek values)
- Error propagation to the UI layer
- Emitting Qt signals when state changes

**Note on naming:** VideoController methods use camelCase (`getFrame`, `getState`) matching Qt conventions.

**Constructor pattern:**

```cpp
class VideoController {
public:
    explicit VideoController(std::unique_ptr<IVideoBackend> backend);
    // ...
private:
    std::unique_ptr<IVideoBackend> backend_;
};

// In production (standalone demo)
auto ctrl = VideoController(std::make_unique<FFmpegBackend>());

// In tests (EV_PLAN Phase 1)
auto ctrl = VideoController(std::make_unique<MockVideoBackend>());
```

---

### 4.3 `MockVideoBackend` — Test Implementation

**Role:** Fake implementation of `IVideoBackend` used exclusively in unit tests (EV_PLAN Phase 1). Simulates all states, transitions, and error conditions without any real video dependency.

**Lives in:** Test codebase only — never shipped in production.

**Capabilities:**
- Configurable responses per method (e.g. force `open()` to return false)
- Call counters (verify that `seek()` was called exactly once)
- Simulated state machine matching the real backend's behavior
- Configurable duration and position values

**Does NOT:**
- Decode any video
- Read any file from disk
- Depend on FFmpeg, Qt Multimedia, or OpenCV

---

### 4.4 `FFmpegBackend` — Production Implementation

**Role:** Production implementation of `IVideoBackend`. Wraps the VideoEngine internal pipeline, exposing it through the `IVideoBackend` facade. `VideoController` never touches any of the internal components directly.

**Technology:** FFmpeg 8.0.1 (via Conan), Qt 6.8.

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

**Threading note:** All IVideoBackend methods (play/pause/stop/seek/open) are called from the Qt main thread by VideoController. FFmpegBackend internally manages worker threads (source, decode). The IVideoBackend API must therefore be thread-safe on entry — seeking must pause worker threads, flush the queue, reposition, and resume.

> **Phase 2+ work. Not yet implemented.**

---

### 4.5 Frame Delivery Model

Two delivery paths exist, each for a different context.

**Pull model** (testing, Phase 1):
- `getFrame() → QImage` called synchronously from VideoController
- Used exclusively with MockVideoBackend — returns a blank QImage
- VideoController guards: returns null QImage from IDLE or ERROR state

**Push model** (production, Phase 2+):
- `connectToSink(QVideoSink*)` registered on FFmpegBackend
- FFmpegBackend's UI-thread timer fires when next frame PTS is due
- Calls `QVideoSink::setVideoFrame(QVideoFrame)` — thread-safe in Qt 6.8
- Qt handles YUV→RGB via built-in GPU shaders
- Demo app: FFmpegBackend → QVideoSink → QVideoWidget (zero-copy path)
- PlotJuggler integration: same push model; PlotJuggler's render widget connects to QVideoSink

The two models are not in conflict — `MockVideoBackend` implements only the pull model; `FFmpegBackend` implements only the push model. `getFrame()` will not be called on a production backend.

---

### 4.6 Threading Model

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

The following states and transitions are valid. Any transition not listed here must result in a defined error — never undefined behavior.

```
                    open(valid)
    IDLE ──────────────────────────► LOADED
     ▲                                  │  stop() → STOPPED
     │  stop()                          │  play()
     │                                  ▼
  STOPPED ◄──────────────────────── PLAYING
     ▲           stop()                 │
     │  stop() [no-op]           pause()│
     │                                  ▼
     │  play() ──────────────────── PAUSED
     │           seek() valid in PLAYING or PAUSED (same state)

  Any state ──── open(invalid) or I/O error ──► ERROR
  ERROR ──── open(valid) ──► LOADED  [recovery path]
```

**Clarifications:**
- `stop()` from `STOPPED` → **no-op** (stay STOPPED, no error)
- `stop()` from `LOADED` → `STOPPED`
- `play()` from `STOPPED` → `PLAYING` (resets position to beginning)
- `REVERSE` is a **direction flag** inside PlaybackController, not a state. Valid values: FORWARD (default) / REVERSE. Direction is only meaningful in PLAYING state.

**Invalid transitions (must produce defined error, not crash):**
- `play()` from `IDLE` (no video loaded)
- `seek(-1)` (negative value)
- `seek(> duration)` (beyond end — clamp or error, behavior must be defined)
- `pause()` when already `PAUSED`
- `getFrame()` from `IDLE` or `ERROR`

**Thread safety:** State transitions inside FFmpegBackend must be guarded (e.g., `std::mutex` or atomic). The adapter (VideoController) reads state after calling backend methods — no race conditions at the IVideoBackend boundary.

---

## 6. File Structure

The merged standalone project structure, combining the VideoEngine internals (the pipeline design) and the adapter layer (Phase 1 implementation):

```
VideoEngine/
├── CMakeLists.txt                    ← C++20, Qt6.8, find FFmpeg
├── conanfile.py                      ← ffmpeg/8.0.1 · mcap/2.1.1 · srt/1.5.4
├── docs/
│   ├── ARCHITECTURE.md               ← this document
│   ├── EV_PLAN.md                    ← evaluation plan
│   ├── PLAN.md                       ← execution plan (reference)
│   └── RESEARCH.md                   ← technology research (reference)
│
├── include/VideoEngine/
│   ├── IVideoBackend.h               ← public API (Phase 1, ✅ implemented)
│   └── PlaybackState.h               ← enum (Phase 1, ✅ implemented)
│
├── src/
│   ├── adapter/
│   │   ├── VideoController.h/.cpp    ← adapter control logic (Phase 1, ✅ implemented)
│   │   └── FFmpegBackend.h/.cpp      ← VideoEngine facade (Phase 2+)
│   ├── core/                         ← VideoEngine internals (Phase 2+)
│   │   ├── types.h                   ← Timestamp · Duration · Codec · PlaybackState
│   │   ├── video_packet.h
│   │   ├── decoded_frame.h           ← RAII AVFrame wrapper
│   │   ├── video_source.h            ← abstract VideoSource interface
│   │   ├── video_decoder.h/.cpp      ← FFmpeg avcodec wrapper
│   │   ├── packet_queue.h            ← SPSC bounded queue
│   │   ├── frame_buffer.h/.cpp       ← ring buffer + LRU + GOP cache
│   │   ├── keyframe_index.h/.cpp     ← sorted keyframe timestamps
│   │   └── playback_controller.h/.cpp← threads + PTS scheduler
│   ├── sources/                      ← VideoSource implementations
│   │   ├── file_video_source.h/.cpp  ← Phase 2
│   │   ├── mcap_video_source.h/.cpp  ← Phase 6
│   │   └── srt_video_source.h/.cpp   ← Phase 7
│   ├── qt/                           ← Qt 6.8 rendering layer
│   │   ├── ffmpeg_video_buffer.h/.cpp← QAbstractVideoBuffer subclass
│   │   ├── video_widget.h/.cpp       ← QWidget: display + controls
│   │   └── timeline_bridge.h/.cpp    ← bidirectional sync interface (Phase 5)
│   └── demo/
│       └── main.cpp                  ← standalone demo app
│
└── tests/
    ├── CMakeLists.txt
    ├── data/                         ← generated test video files (Phase 2+)
    │   └── test_coarse.mp4
    ├── MockVideoBackend.h/.cpp       ← Phase 1 (✅ implemented)
    ├── test_main.cpp                 ← Phase 1 (✅ implemented)
    ├── test_video_selection.cpp      ← Phase 1 (✅ implemented)
    ├── test_state_transitions.cpp    ← Phase 1 (✅ implemented)
    ├── test_seek.cpp                 ← Phase 1 (✅ implemented)
    ├── test_robustness.cpp           ← Phase 1 (✅ implemented)
    ├── test_video_decoder.cpp        ← Phase 3+ (VideoEngine internals)
    ├── test_frame_buffer.cpp         ← Phase 3+ (VideoEngine internals)
    ├── test_packet_queue.cpp         ← Phase 3+ (VideoEngine internals)
    └── test_keyframe_index.cpp       ← Phase 3+ (VideoEngine internals)
```

---

## 7. Build System

**Language standard:** C++20.

**Framework:** CMake 3.20+.

**Qt version:** Qt 6.8.

**Minimum CMake structure:**

```cmake
cmake_minimum_required(VERSION 3.20)
project(VideoEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Multimedia MultimediaWidgets)
find_package(FFmpeg REQUIRED)          # via Conan: ffmpeg/8.0.1
# find_package(mcap REQUIRED)          # Phase 6: mcap/2.1.1
# find_package(srt REQUIRED)           # Phase 7: srt/1.5.4
```

**Dependencies:** Managed via `conanfile.py` (Python format — not `.txt`).

**GTest:** `find_package(GTest QUIET)` — system package. Tests are conditional:

```cmake
if(GTest_FOUND)
    add_executable(video_unit_tests
        tests/MockVideoBackend.cpp
        tests/test_video_selection.cpp
        tests/test_state_transitions.cpp
        tests/test_seek.cpp
        tests/test_robustness.cpp
    )
    target_link_libraries(video_unit_tests GTest::GTest GTest::Main)
endif()
```

---

## 8. FPS Reference & Temporal Parameters

These values are agreed and used across both `ARCHITECTURE.md` and `EV_PLAN.md`.

| Parameter | Value | Notes |
|---|---|---|
| Reference FPS | 30 FPS | Agreed |
| Frame duration | 33,333 µs | 1,000,000 / 30 |
| Fine tolerance (N=1) | 33,333 µs | EV_PLAN Phase 3 |
| Timestamp type | `Timestamp` (`int64_t µs`) | from types.h |
| Seek unit | **microseconds** | Current standard |

---

## 9. Resolved Decisions

| # | Decision | Resolution |
|---|---|---|
| 1 | Video decoding library | FFmpeg 8.0.1 via Conan |
| 2 | Frame delivery model | Push via QVideoSink (production); pull getFrame() for test mocks |
| 3 | Project directory | Standalone `VideoEngine/` project (see Section 6) |
| 4 | Qt version | Qt 6.8 (standalone); Qt5↔Qt6 boundary resolved at PlotJuggler integration |
| 5 | GTest integration | `find_package(GTest QUIET)` — system package |
| 6 | Test video format | MP4, H.264, CFR 30fps, 1080p, 30s (Phase 2); MCAP + MKV in Phases 6+ |
| 7 | C++ standard | C++20 (VideoEngine library); adapter layer is C++17 compatible |
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

