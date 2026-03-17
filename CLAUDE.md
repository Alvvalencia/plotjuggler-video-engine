# VideoEngine — Project Context

## What this is

A standalone **C++20 library + Qt 6.8 application** for frame-accurate video playback with seeking and reverse playback. Designed to eventually integrate into [PlotJuggler](https://github.com/facontidavide/PlotJuggler) as a `StatePublisher` plugin.

**Current status:** Documentation and architecture phase complete. No source code yet — implementation starts at Phase 1 (adapter layer + mock backend + unit tests).

---

## Stack

| Layer | Technology |
|-------|-----------|
| Language | C++20 (core) |
| Build | CMake 3.20+ |
| Dependencies | Conan (`conanfile.py`) |
| Video decode | FFmpeg 8.0.1 |
| UI / display | Qt 6.8 (Core, Gui, Multimedia, MultimediaWidgets) |
| Testing | Google Test (GTest) |
| Future sources | MCAP 2.1.1 (Phase 6), libsrt 1.5.4 (Phase 7) |

---

## Architecture summary

```
VideoController  ──depends on──►  IVideoBackend (pure virtual)
                                        │
                              ┌─────────┴──────────┐
                              ▼                     ▼
                      MockVideoBackend        FFmpegBackend
                       (tests, Phase 1)     (production, Phase 2+)
                                                    │
                                           PlaybackController
                                           (3-thread pipeline)
```

- **Dependency Inversion** is the central design principle — `VideoController` never touches FFmpeg directly
- **State machine:** IDLE → LOADED → PLAYING ↔ PAUSED → STOPPED, plus ERROR from any state
- **Time unit:** microseconds (`int64_t`) throughout the codebase
- **Threading:** `VideoController` on Qt main thread; `FFmpegBackend` manages its own source/decode threads internally

Key docs:
- `docs/ARCHITECTURE.md` — component design and interfaces
- `docs/PLAN.md` — 8-phase execution plan
- `docs/RESEARCH.md` — technology decisions and rationale
- `docs/EVALUATION.md` — test phases and pass criteria

---

## Phased implementation plan

| Phase | Scope | Status |
|-------|-------|--------|
| 1 | Adapter layer (`IVideoBackend`, `VideoController`), `MockVideoBackend`, 24 unit tests | **Next** |
| 2 | `FFmpegBackend` (file decode, `FileVideoSource`), Qt display widget | Pending |
| 3 | Reverse playback, keyframe index, frame buffer | Pending |
| 4 | Multi-format support, resolution/FPS variations | Pending |
| 5 | PlotJuggler timeline sync (`TimelineBridge`) | Pending |
| 6 | MCAP source (`McapVideoSource`) | Pending |
| 7 | Live SRT stream source | Pending |

---

## Testing approach

- Phase 1 uses **MockVideoBackend only** — zero external dependencies, no video files required
- GTest is the framework; conditional compilation (`if(GTest_FOUND)`)
- Phase 1 test groups: video selection, state transitions, seeking, robustness
- Real video integration tests begin in Phase 2 with `tests/data/test_coarse.mp4`

---

## File structure (planned — nothing exists yet)

```
VideoEngine/
├── CMakeLists.txt
├── conanfile.py
├── include/VideoEngine/
│   ├── IVideoBackend.h      ← public API boundary
│   └── PlaybackState.h
├── src/
│   ├── adapter/             ← VideoController, FFmpegBackend
│   ├── core/                ← types, packet queue, frame buffer, decoder
│   ├── sources/             ← FileVideoSource, McapVideoSource, SrtVideoSource
│   └── qt/                  ← FFmpegVideoBuffer, VideoWidget, TimelineBridge
├── tests/
│   ├── MockVideoBackend.h/.cpp
│   ├── test_video_selection.cpp
│   ├── test_state_transitions.cpp
│   ├── test_seek.cpp
│   └── test_robustness.cpp
└── docs/
```

---

## Structural files — always ask before modifying

- `CMakeLists.txt`, `conanfile.py`
- Any file under `include/VideoEngine/` (public API)
- `docs/` files

---

## Open questions / gaps to fill

> **User:** Please fill in any missing context below so future sessions start correctly.

- [ ] Target platform(s): Linux only? Ubuntu version?
- [ ] PlotJuggler version targeted for integration?
- [ ] Any FFmpeg build flags or codec restrictions?
- [ ] Preferred test runner invocation (`ctest`? direct binary?)
- [ ] Any coding style constraints beyond C++20 standard practices?
