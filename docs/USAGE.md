# VideoEngine — Usage Guide

## Prerequisites

| Dependency | Version | How to install |
|-----------|---------|---------------|
| C++ compiler | C++20 support (GCC 11+, Clang 14+) | System package manager |
| CMake | 3.20+ | `sudo apt install cmake` |
| Qt 6.8 | 6.8.x | [aqtinstall](https://github.com/miurahr/aqtinstall) or system package |
| FFmpeg dev libs | 6.x+ | `sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev` |
| Conan | 2.x | `pip install conan` |
| pkg-config | any | `sudo apt install pkg-config` |

On Ubuntu 24.04, the FFmpeg dev packages are available directly:

```bash
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
```

---

## Building

### 1. Install Conan dependencies (GTest only)

```bash
cd VideoEngine
conan install . --output-folder=build --build=missing
```

### 2. Generate test video

The test suite requires a synthetic video file. Generate it once:

```bash
bash tests/generate_test_data.sh
```

This creates `tests/data/test_coarse.mp4` (30s, 1080p, H.264, 30fps, GOP=30 — ~120 KB).

### 3. Configure and build (Release)

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
  -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64/ \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build -j$(nproc)
```

Adjust `CMAKE_PREFIX_PATH` to your Qt installation path.

### 4. Build with AddressSanitizer (optional)

```bash
conan install . --output-folder=build-asan --build=missing

cmake -S . -B build-asan \
  -DCMAKE_TOOLCHAIN_FILE=build-asan/conan_toolchain.cmake \
  -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64/ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"

cmake --build build-asan -j$(nproc)
```

---

## Running Tests

### All tests

```bash
cd build
ctest --output-on-failure
```

### Verbose output (individual test results)

```bash
./video_unit_tests --gtest_print_time=1
```

### Run a specific test group

```bash
# Phase 1 — mock-based unit tests
./video_unit_tests --gtest_filter="VideoSelectionTest.*"
./video_unit_tests --gtest_filter="StateTransitionTest.*"
./video_unit_tests --gtest_filter="SeekTest.*"
./video_unit_tests --gtest_filter="RobustnessTest.*"

# Phase 2 — FFmpeg integration
./video_unit_tests --gtest_filter="FileVideoSourceTest.*"
./video_unit_tests --gtest_filter="VideoDecoderTest.*"
./video_unit_tests --gtest_filter="PacketQueueTest.*"
./video_unit_tests --gtest_filter="FFmpegBackendTest.*"

# Phase 3 — seeking and frame stepping
./video_unit_tests --gtest_filter="KeyframeIndexTest.*"
./video_unit_tests --gtest_filter="FrameBufferTest.*"
./video_unit_tests --gtest_filter="SeekingTest.*"
```

### Run a single test

```bash
./video_unit_tests --gtest_filter="SeekingTest.StepForward"
```

### ASan tests

```bash
cd build-asan
ctest --output-on-failure
```

---

## Demo Application

The demo app provides a GUI for manually testing video playback.

### Launch

```bash
cd build
./video_demo <video_file>

# Example with test video:
./video_demo tests/data/test_coarse.mp4
```

### Controls

| Control | Action |
|---------|--------|
| **Play/Pause button** | Toggle playback |
| **`<\|` button** | Step one frame backward |
| **`\|>` button** | Step one frame forward |
| **Seek slider** | Drag and release to seek to position |
| **Time label** | Shows current position / total duration |

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Space` | Play / Pause toggle |
| `Right` | Step forward one frame |
| `Left` | Step backward one frame |
| `Shift+Right` | Seek forward 1 second |
| `Shift+Left` | Seek backward 1 second |

### What to verify manually

The test video displays a counter (0–29), one number per second. This makes verification visual:

- **Play/Pause**: numbers increment smoothly at 1x speed; pause freezes the counter
- **Step forward**: counter advances to the next frame (sub-second precision)
- **Step backward**: counter goes back to the previous frame
- **Seek slider**: drag to second 15 — counter shows "15"
- **Shift+Arrow**: jumps exactly 1 second forward/backward

---

## Test Summary (59 tests)

| Phase | File | Tests | What it validates |
|-------|------|------:|-------------------|
| 1 | `test_video_selection.cpp` | 4 | open valid/invalid/empty paths, replace loaded video |
| 1 | `test_state_transitions.cpp` | 7 | play/pause/stop from all states, no-ops, invalid transitions |
| 1 | `test_seek.cpp` | 6 | seek valid/invalid/clamp/during-play/during-pause |
| 1 | `test_robustness.cpp` | 4 | illegal call order, destructor safety, rapid seek |
| 2 | `test_video_decoder.cpp` | 8 | FileVideoSource, VideoDecoder, DecodedFrame RAII, FFmpegVideoBuffer |
| 2 | `test_packet_queue.cpp` | 6 | push/pop, FIFO order, clear, shutdown, concurrent access |
| 2 | `test_ffmpeg_backend.cpp` | 4 | FFmpegBackend open/play/pause/position, controller integration |
| 3 | `test_keyframe_index.cpp` | 6 | keyframe scan, nearestBefore/After, empty index |
| 3 | `test_frame_buffer.cpp` | 6 | push/retrieve, eviction, clear, concurrent access |
| 3 | `test_seeking.cpp` | 8 | seek to 0/15s/29s, seek during playback, step fwd/back, multi-seek |

---

## Project Status

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Adapter layer, MockVideoBackend, unit tests | Complete (21 tests) |
| 2 | FFmpegBackend, decode pipeline, Qt display | Complete (18 tests) |
| 3 | Seeking, keyframe index, frame buffer, stepping | Complete (20 tests) |
| 4 | Reverse playback, speed control (0.25x–4x) | Next |
| 5 | PlotJuggler timeline sync | Pending |
| 6 | MCAP video source | Pending |
| 7 | Live SRT streaming | Pending |
| 8 | Hardware acceleration | Pending |
