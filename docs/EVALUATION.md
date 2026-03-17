# VideoEngine — Evaluation Plan
*Standalone Library, C++20 / Qt 6.8 / FFmpeg 8.0.1*

---

## Metadata

| Field | Value |
|---|---|
| Project | VideoEngine Standalone Library |
| Version | 2.0 |
| Date | March 2026 |
| Reference FPS | 30 FPS |
| Test framework | Google Test (GTest) — C++20 |
| C++ standard | C++20 (VideoEngine core) · C++17 compatible (adapter layer) |
| Qt version | 6.8 |
| FFmpeg | 8.0.1 |
| Time unit | microseconds (int64_t) |
| Overall status | Phase 1 IMPLEMENTED · Phases 2–6 UNBLOCKED (conditionally) · Phase 7 DEFERRED |

---

## Executive Summary

This document defines the complete evaluation plan for the VideoEngine standalone library. It is designed to be incremental: each phase can be executed independently and unlocks the next.

**Core principles:**
- Nothing is developed that cannot be tested.
- No phase is started without the previous one closed.
- Every test video has exact, reproducible specifications.

---

## Architecture Summary

The VideoEngine project is composed of two layers. Refer to `ARCHITECTURE.md` for full detail.

### `IVideoBackend`
Pure abstract C++ class that defines the video backend contract. The control layer only knows this interface — never the concrete implementation.

Methods: `open(path)` | `play()` | `pause()` | `stop()` | `seek(t_us)` | `getFrame()` | `getState()`

> **Time unit note:** The seek unit is **microseconds** throughout the project.

### `MockVideoBackend`
Fake implementation of `IVideoBackend` for tests. No dependency on any real video library. Simulates states, errors, and seek behavior with predefined values.

### `VideoController`
Adapter class containing all playback control logic (play/pause/seek/stop). Receives an `IVideoBackend` via dependency injection. This is the class under test in Phase 1.

At integration time, `VideoController` becomes a `PJ::StatePublisher` adapter — see Phase 7.

### `FFmpegBackend` (Phase 2+)
Production implementation of `IVideoBackend` using FFmpeg 8.0.1. Wraps the VideoEngine internal pipeline (PlaybackController, VideoSource, PacketQueue, VideoDecoder, FrameBuffer). `IVideoBackend` is the only coupling point — VideoController never touches the internals.

---

## PHASE 1 — Unit Tests `✅ IMPLEMENTED`

| Field | Value |
|---|---|
| Goal | Validate control logic without any real video or external dependency |
| Prerequisite | None |
| Backend | MockVideoBackend |
| Framework | Google Test (GTest) via CMake |
| Reference FPS | 30 FPS (33,333 µs per frame) |

### 1.1 — Test cases: Video selection

| ID | Action | Expected result | Pass criterion |
|---|---|---|---|
| UT-SEL-01 | `open()` with valid path (simulated) | State transitions to `LOADED` | `EXPECT_EQ(state, LOADED)` |
| UT-SEL-02 | `open()` with empty path `""` | Throws or returns error | `EXPECT_THROW` or `EXPECT_FALSE` |
| UT-SEL-03 | `open()` with invalid path | State stays `IDLE`, error reported | `EXPECT_EQ(state, IDLE)` |
| UT-SEL-04 | `open()` when video already loaded | Replaces previous video without crash | No exception, state `LOADED` |

### 1.2 — Test cases: State transitions

| ID | Action | Expected result | Pass criterion |
|---|---|---|---|
| UT-ST-01 | `play()` after valid `open()` | State transitions to `PLAYING` | `EXPECT_EQ(state, PLAYING)` |
| UT-ST-02 | `pause()` during `PLAYING` | State transitions to `PAUSED` | `EXPECT_EQ(state, PAUSED)` |
| UT-ST-03 | `play()` after `pause()` | State returns to `PLAYING` | `EXPECT_EQ(state, PLAYING)` |
| UT-ST-04 | `stop()` from any active state | State transitions to `STOPPED` | `EXPECT_EQ(state, STOPPED)` |
| UT-ST-05 | `play()` without loaded video | No crash, error reported | `EXPECT_FALSE(result)` |
| UT-ST-06 | `pause()` when already `PAUSED` | State stays `PAUSED` | `EXPECT_EQ(state, PAUSED)` |
| UT-ST-07 | `play()` after `stop()` | State transitions to `PLAYING` from beginning | `EXPECT_EQ(state, PLAYING)` |

### 1.3 — Test cases: Seek logic

| ID | Action | Expected result | Pass criterion |
|---|---|---|---|
| UT-SK-01 | `seek(0)` | Position at 0 ms | `EXPECT_EQ(pos_ms, 0)` |
| UT-SK-02 | `seek(5000)` with duration 10000 ms | Position at 5000 ms | `EXPECT_EQ(pos_ms, 5000)` |
| UT-SK-03 | `seek(-1)` | Error / exception, position unchanged | `EXPECT_THROW` or pos unchanged |
| UT-SK-04 | `seek(99999)` beyond end | Clamp to last frame or error | No crash, defined behavior |
| UT-SK-05 | `seek()` during `PLAYING` | Continues playing from new position | `EXPECT_EQ(state, PLAYING)` |
| UT-SK-06 | `seek()` during `PAUSED` | Position updated, stays `PAUSED` | `EXPECT_EQ(state, PAUSED)` |

### 1.4 — Test cases: Robustness and errors

| ID | Action | Expected result | Pass criterion |
|---|---|---|---|
| UT-RB-01 | `getFrame()` without loaded video | Returns null or empty frame | `EXPECT_EQ(frame, nullptr)` |
| UT-RB-02 | Illegal order calls (`play→play`) | No crash, consistent state | No exception thrown |
| UT-RB-03 | Destructor with video in `PLAYING` | Clean shutdown, no leak or crash | No crash in destructor |
| UT-RB-04 | Multiple rapid `seek()` calls | Consistent state at end | Position == last seek |

### 1.5 — Phase 1 pass criteria

- 100% of tests pass without errors.
- 0 crashes (including in destructor and error cases).
- 0 memory leaks detected with Valgrind or AddressSanitizer.
- All illegal states produce defined behavior (no undefined behavior).
- `MockVideoBackend` has zero external dependencies (no FFmpeg, no Qt Multimedia).

---

## PHASE 2 — Integration Tests (Coarse) `🔓 READY`

> **Unblocked** — technology confirmed: FFmpeg 8.0.1, H.264, MP4, 1080p, 30 FPS CFR.

**Goal:** Validate that `FFmpegBackend` correctly decodes and seeks in a real MP4 file. Tests run in the standalone VideoEngine project.

Specifically: `seek(t_us)` must return the correct second in a video with per-second visual markers.

### Video 1 Specification

| Parameter | Value |
|---|---|
| Format | MP4 (container) |
| Codec | H.264 (libx264) |
| Resolution | 1920×1080 (1080p) |
| FPS | 30 CFR (constant frame rate) |
| Duration | 30 seconds |
| Content | Black background, white centered number incrementing 1 per second |
| GOP size | 30 frames (1 keyframe per second) |

**Generation command (FFmpeg):**

```bash
ffmpeg -f lavfi -i "color=black:size=1920x1080:rate=30,drawtext=text='%{n}':fontsize=120:fontcolor=white:x=(w-text_w)/2:y=(h-text_h)/2" \
       -t 30 -vcodec libx264 -preset fast -pix_fmt yuv420p -g 30 test_coarse.mp4
```

**Video file location:** `tests/data/test_coarse.mp4` — generate before running Phase 2 tests.

**Validation tolerance:** `seek(N_us)` must return the frame with number == N (seconds).
Seek unit in Phase 2 test assertions: **microseconds** (after migration).

---

## PHASE 3 — Fine Temporal Tests `🔓 Unblocked pending Phase 2`

> **Prerequisite:** Phase 2 complete.

**Goal:** Validate precision at the individual frame level with error < 1 frame (33,333 µs at 30 FPS).

### Video 2 Specification

Each frame visually renders its `frame_id` and `time_us`. The tolerance formula is:

```
tolerance_us = N × (1_000_000 / FPS)
With FPS=30 and N=1 → tolerance_us = 33_333 µs
```

The value of N will be fixed at 1 unless Phase 2 results justify increasing it.

**Test assertion format (after migration):**

```cpp
EXPECT_NEAR(pos_us, target_us, 33333);
```

Seek unit for Phase 3: **microseconds**.

---

## PHASE 4 — Multi-Video Tests `🔓 Unblocked pending Phase 3`

> **Prerequisite:** Phase 3 complete.

**Goal:** Validate consistent behavior across formats, resolutions, and framerates.

### Planned cases
- MP4 / MKV / **MCAP** / AVI — same content, different containers.
- CFR (constant frame rate) vs VFR (variable frame rate).
- 24 FPS / 30 FPS / 60 FPS.
- Resolutions: 480p / 720p / 1080p.

> Test case definitions will be structured in JSON. Exact schema to be defined when this phase is unblocked.
>
> **Note:** MCAP format requires `McapVideoSource` with Conan dependency `mcap/2.1.1` (active from Phase 6 of PLAN.md).

---

## PHASE 5 — Robustness Tests `🔓 Unblocked pending Phase 2`

> **Prerequisite:** Phase 2 complete (real FFmpegBackend available).

**Goal:** Validate `FFmpegBackend` error handling in the standalone context.

### Planned cases
- Corrupted file (damaged header).
- Non-existent file on disk.
- Unsupported format.
- `seek()` during file loading.
- Forced close during active playback.
- Connection lost mid-stream (SRT source) — Phase 7+.

---

## PHASE 6 — Performance Tests `🔓 Unblocked pending Phase 3 + hardware`

> **Prerequisite:** Phase 3 complete + reference hardware agreed.

**Goal:** Measure open time, seek latency, decode throughput, and memory usage against defined acceptance thresholds.

### Metrics

| Metric | Reference threshold | Source |
|---|---|---|
| Seek latency (MP4, 1s GOPs) | **< 200 ms** | RESEARCH.md |
| Decode throughput H.264 1080p (software) | **200–400 FPS** | RESEARCH.md |
| Memory usage per GOP at 1080p YUV420P | **~93 MB** | RESEARCH.md |
| File open time | To be defined with hardware | — |
| Sustained FPS in continuous playback | To be defined with hardware | — |

> Hardware acceleration (Phase 8 of PLAN.md) expected to improve decode throughput to 3–5× baseline.
>
> Exact acceptance thresholds will be finalized when reference hardware is confirmed.

---

## PHASE 7 — PlotJuggler Integration `🔒 DEFERRED`

> **Status:** DEFERRED — after Phase 2 complete + PlotJuggler Qt version confirmed.

**Goal:** Integrate VideoEngine into PlotJuggler as a `StatePublisher` plugin.

### Items
- Implement `PublisherVideo : public PJ::StatePublisher`
- Connect `updateState(double time_s)` → `VideoController::seek(static_cast<int64_t>(time_s * 1e6))`
- Implement `TimelineBridge` for bidirectional synchronization (PlotJuggler ↔ VideoEngine):
  - `videoTimeChanged(Timestamp)` → PlotJuggler timeline
  - `onExternalTimeChanged(Timestamp)` → seek
  - `onExternalPlaybackCommand(PlaybackState)` → play/pause
- Implement `xmlSaveState()` / `xmlLoadState()` for session persistence (last-opened video path)
- Resolve Qt5↔Qt6 compatibility (PlotJuggler may be Qt6 by then, or process isolation may be needed)
- Build as Qt plugin: `Q_PLUGIN_METADATA`, `QT_PLUGIN` flag, install to `${PJ_PLUGIN_INSTALL_DIRECTORY}`

---

## Order of Execution and Dependencies

| Phase | Status | Blocker |
|---|---|---|
| Phase 1 — Unit Tests | ✅ IMPLEMENTED | — |
| Phase 2 — Integration Coarse | 🔓 READY | — |
| Phase 3 — Fine Temporal | 🔓 Conditional | Phase 2 complete |
| Phase 4 — Multi-Video | 🔓 Conditional | Phase 3 + MCAP Conan dep |
| Phase 5 — Robustness | 🔓 Conditional | Phase 2 complete |
| Phase 6 — Performance | 🔓 Conditional | Phase 3 + hardware agreed |
| Phase 7 — PlotJuggler Integration | 🔒 DEFERRED | Phase 2 complete + PJ Qt version confirmed |

