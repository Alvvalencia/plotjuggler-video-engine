# VideoEngine — Evaluation Plan
*Standalone Library, C++20 / Qt 6.8 / FFmpeg 6.1.1 (system)*

---

## Metadata

| Field | Value |
|---|---|
| Project | VideoEngine Standalone Library |
| Version | 2.0 |
| Date | March 2026 |
| Reference FPS | 30 FPS |
| Test framework | Google Test (GTest) — C++20 |
| C++ standard | C++20 |
| Qt version | 6.8 |
| FFmpeg | 6.1.1 (system, via pkg-config) |
| Time unit | microseconds (int64_t) |
| Overall status | Phases 1–3 complete (59/59 tests, ASan clean) · Phase 4 next · Phases 5–7 pending |

---

## Executive Summary

This document defines the complete evaluation plan for the VideoEngine standalone library. It is designed to be incremental: each phase can be executed independently and unlocks the next.

**Core principles:**
- Nothing is developed that cannot be tested.
- No phase is started without the previous one closed.
- Every test video has exact, reproducible specifications.

---

## Architecture Reference

All interfaces, state machine, and component descriptions live in `ARCHITECTURE.md`. This document only defines test cases and pass criteria.

**Key facts for testers:** `VideoController` is the class under test. It wraps an `IVideoBackend` (mock or real) via dependency injection. All time units are **microseconds** (`int64_t`). Redundant operations (e.g. `pause()` when already PAUSED) are no-ops, not errors.

---

## PHASE 1 — Unit Tests `COMPLETE`

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
| UT-SEL-02 | `open()` with empty path `""` | Returns false, state stays `IDLE` | `EXPECT_FALSE(result)` && `EXPECT_EQ(state, IDLE)` |
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
| UT-ST-06 | `pause()` when already `PAUSED` | No-op, returns true, stays `PAUSED` | `EXPECT_TRUE(result)` && `EXPECT_EQ(state, PAUSED)` |
| UT-ST-07 | `play()` after `stop()` | State transitions to `PLAYING` from beginning | `EXPECT_EQ(state, PLAYING)` |

### 1.3 — Test cases: Seek logic

| ID | Action | Expected result | Pass criterion |
|---|---|---|---|
| UT-SK-01 | `seek(0)` | Position at 0 µs | `EXPECT_EQ(pos_us, 0)` |
| UT-SK-02 | `seek(5'000'000)` with duration 10'000'000 µs | Position at 5'000'000 µs (5 s) | `EXPECT_EQ(pos_us, 5'000'000)` |
| UT-SK-03 | `seek(-1)` | Returns false, position unchanged | `EXPECT_FALSE(result)` && pos unchanged |
| UT-SK-04 | `seek(99'000'000)` beyond end | Clamp to `duration - one_frame`, return true | `EXPECT_TRUE(result)` && `EXPECT_EQ(pos_us, duration - 33'333)` |
| UT-SK-05 | `seek()` during `PLAYING` | Continues playing from new position | `EXPECT_EQ(state, PLAYING)` |
| UT-SK-06 | `seek()` during `PAUSED` | Position updated, stays `PAUSED` | `EXPECT_EQ(state, PAUSED)` |

### 1.4 — Test cases: Robustness and errors

| ID | Action | Expected result | Pass criterion |
|---|---|---|---|
| UT-RB-01 | `seek(5'000'000)` without loaded video | Returns error, state stays IDLE | `EXPECT_FALSE(result)` |
| UT-RB-02 | Illegal order calls (`play→play`) | No crash, consistent state | No exception thrown |
| UT-RB-03 | Destructor with video in `PLAYING` | Clean shutdown, no leak or crash | No crash in destructor |
| UT-RB-04 | Multiple rapid `seek()` calls | Consistent state at end | Position == last seek |

### 1.5 — Phase 1 pass criteria

- 100% of tests pass without errors.
- 0 crashes (including in destructor and error cases).
- 0 memory leaks detected with Valgrind or AddressSanitizer.
- All illegal states produce defined behavior (no undefined behavior).
- `MockVideoBackend` has no FFmpeg dependency (Qt Core/Gui/Multimedia required for interface types).

**Result:** 21/21 tests passing, ASan clean. All criteria met.

---

## PHASE 2 — Integration Tests (Coarse) `COMPLETE`

> Technology confirmed: FFmpeg 6.1.1 (system), H.264, MP4, 1080p, 30 FPS CFR.

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
ffmpeg -f lavfi -i "color=black:size=1920x1080:rate=30,drawtext=text='%{eif\:floor(t)\:d}':fontsize=120:fontcolor=white:x=(w-text_w)/2:y=(h-text_h)/2" \
       -t 30 -vcodec libx264 -preset fast -pix_fmt yuv420p -g 30 test_coarse.mp4
```

> **Note:** `%{eif\:floor(t)\:d}` renders the current second as an integer (0, 1, 2, ..., 29). Do NOT use `%{n}` — that gives frame numbers (0–899), not seconds.

**Video file location:** `tests/data/test_coarse.mp4` — generate via `tests/generate_test_data.sh`.

**Validation tolerance:** `seek(N * 1'000'000)` must return the frame showing second N.
Seek unit: **microseconds** (`int64_t`) — consistent with the rest of the project.

**Result:** 18 Phase 2 tests passing (DEC-01..08, PQ-01..06, FB-01..04). 39/39 cumulative. ASan clean.

---

## PHASE 3 — Fine Temporal Tests `COMPLETE`

> Phase 3 tests validate frame-accurate seeking (±1 frame = 33,333 µs), step forward/backward, keyframe index, and frame buffer.

**Goal:** Validate precision at the individual frame level with error < 1 frame (33,333 µs at 30 FPS).

### Video 2 Specification

Each frame visually renders its `frame_id` and `time_us`. The tolerance formula is:

```
tolerance_us = N × (1_000_000 / FPS)
With FPS=30 and N=1 → tolerance_us = 33_333 µs
```

The value of N will be fixed at 1 unless Phase 2 results justify increasing it.

**Test assertion format:**

```cpp
EXPECT_NEAR(pos_us, target_us, 33333);
```

**Tests implemented (20 tests across 3 files):**

| File | ID | Test | Verification |
|------|----|------|-------------|
| `test_keyframe_index.cpp` | KF-01 | Build index from test video | ~25–35 keyframes, first near PTS 0 |
| | KF-02 | nearestBefore midpoint (15.5s) | Returns keyframe at ~15s |
| | KF-03 | nearestBefore(0) | Returns first keyframe |
| | KF-04 | nearestBefore past end | Returns last keyframe (~29s) |
| | KF-05 | nearestAfter midpoint (10.5s) | Returns keyframe at ~11s |
| | KF-06 | Empty index | Returns nullopt |
| `test_frame_buffer.cpp` | FB-01 | Push and retrieve by PTS | frameBefore returns correct frame |
| | FB-02 | frameAfter returns next frame | Correct PTS ordering |
| | FB-03 | frameBefore with no match | Returns nullopt |
| | FB-04 | Eviction on budget overflow | Frame count < 5 with tiny budget |
| | FB-05 | clear empties buffer | empty() == true, frameCount() == 0 |
| | FB-06 | Concurrent push + read | No crash, no deadlock |
| `test_seeking.cpp` | SK-01 | Seek to second 0 | Position within ±33,333 µs of 0 |
| | SK-02 | Seek to second 15 | Position within ±33,333 µs of 15s |
| | SK-03 | Seek to second 29 | Position within ±33,333 µs of 29s |
| | SK-04 | Seek during playback | Position updates correctly after seek |
| | SK-05 | Step forward | Position advances by ~1 frame |
| | SK-06 | Step backward | Position goes back by ~1 frame |
| | SK-07 | Multiple seeks (5 targets) | All within tolerance |
| | SK-08 | Controller + step integration | State is PAUSED after stepping |

**Result:** 20 Phase 3 tests passing. 59/59 cumulative. ASan clean.

---

## PHASE 4 — Multi-Video Tests `🔓 Unblocked`

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

## PHASE 5 — Robustness Tests `🔓 Unblocked`

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

## PHASE 6 — Performance Tests `🔓 Unblocked`

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

| Eval Phase | Maps to PLAN.md | Status | Blocker |
|---|---|---|---|
| Phase 1 — Unit Tests | Phase 1 (Adapter + Scaffold) | COMPLETE (21 tests) | — |
| Phase 2 — Integration Coarse | Phase 2 (Core Types + File Playback) | COMPLETE (18 tests) | — |
| Phase 3 — Fine Temporal | Phase 3 (Seeking + Frame Stepping) | COMPLETE (20 tests) | — |
| Phase 4 — Multi-Video | Phase 6 (MCAP Source) | 🔓 Unblocked | Phase 3 complete; needs MCAP dep |
| Phase 5 — Robustness | Phase 2+ | 🔓 Unblocked | — |
| Phase 6 — Performance | Phase 3+ | 🔓 Unblocked | Needs hardware agreed |
| Phase 7 — PlotJuggler Integration | Phase 5 (Timeline Sync) | 🔒 DEFERRED | PJ Qt version confirmed |

