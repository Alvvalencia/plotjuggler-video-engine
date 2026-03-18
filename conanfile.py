from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class VideoEngineConan(ConanFile):
    name = "videoengine"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"

    # FFmpeg is found via system pkg-config (libavformat-dev, etc.)
    # to avoid Conan's heavy transitive dependency chain (xorg, pulseaudio).
    # System FFmpeg 6.1.1 APIs are identical to 7.x for our use case.
    requires = ("gtest/1.15.0",)

    def configure(self):
        self.settings.compiler.cppstd = "20"

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()
