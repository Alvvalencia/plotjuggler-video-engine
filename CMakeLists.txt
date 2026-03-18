cmake_minimum_required(VERSION 3.20)
project(VideoEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Multimedia Widgets MultimediaWidgets)
find_package(GTest REQUIRED)
find_package(PkgConfig REQUIRED)

# FFmpeg via system pkg-config
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC  REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL   REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE  REQUIRED IMPORTED_TARGET libswscale)

# --- Library ---
add_library(VideoEngine
    src/VideoController.cpp
    src/core/decoded_frame.cpp
    src/core/packet_queue.cpp
    src/core/playback_controller.cpp
    src/core/keyframe_index.cpp
    src/core/frame_buffer.cpp
    src/sources/file_video_source.cpp
    src/core/video_decoder.cpp
    src/qt/ffmpeg_video_buffer.cpp
    src/qt/video_widget.cpp
    src/FFmpegBackend.cpp
    include/VideoEngine/VideoController.h
)
target_include_directories(VideoEngine PUBLIC include)
target_link_libraries(VideoEngine
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Widgets Qt6::MultimediaWidgets
    PRIVATE PkgConfig::AVFORMAT PkgConfig::AVCODEC
            PkgConfig::AVUTIL PkgConfig::SWSCALE
)

# --- Demo ---
add_executable(video_demo demo/main.cpp)
target_link_libraries(video_demo
    PRIVATE VideoEngine Qt6::Widgets Qt6::MultimediaWidgets
            PkgConfig::AVFORMAT PkgConfig::AVCODEC PkgConfig::AVUTIL
)

# --- Tests ---
enable_testing()

add_executable(video_unit_tests
    tests/test_main.cpp
    tests/MockVideoBackend.cpp
    tests/test_video_selection.cpp
    tests/test_state_transitions.cpp
    tests/test_seek.cpp
    tests/test_robustness.cpp
    tests/test_video_decoder.cpp
    tests/test_packet_queue.cpp
    tests/test_ffmpeg_backend.cpp
    tests/test_keyframe_index.cpp
    tests/test_frame_buffer.cpp
    tests/test_seeking.cpp
)
target_link_libraries(video_unit_tests
    PRIVATE VideoEngine GTest::gtest PkgConfig::AVUTIL
)

add_test(NAME video_unit_tests COMMAND video_unit_tests)
