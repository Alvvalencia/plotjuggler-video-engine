#ifndef VIDEOENGINE_CORE_DECODED_FRAME_H
#define VIDEOENGINE_CORE_DECODED_FRAME_H

#include <cstdint>

struct AVFrame; // forward-declare to avoid leaking FFmpeg headers everywhere

namespace videoengine {

// RAII wrapper around AVFrame with value semantics.
// Copy uses av_frame_ref (refcount), move steals the pointer.
class DecodedFrame {
public:
    DecodedFrame();
    ~DecodedFrame();

    DecodedFrame(const DecodedFrame& other);
    DecodedFrame& operator=(const DecodedFrame& other);

    DecodedFrame(DecodedFrame&& other) noexcept;
    DecodedFrame& operator=(DecodedFrame&& other) noexcept;

    // Wrap an existing AVFrame (copies via av_frame_ref — refcounted, not deep copy)
    static DecodedFrame fromAVFrame(const AVFrame* src);

    int width() const;
    int height() const;
    int pixelFormat() const;  // AVPixelFormat cast to int
    int64_t pts() const;

    uint8_t* const* data() const;
    const int* linesize() const;

    AVFrame* raw();
    const AVFrame* raw() const;
    bool isValid() const;

private:
    AVFrame* frame_ = nullptr;
};

} // namespace videoengine

#endif // VIDEOENGINE_CORE_DECODED_FRAME_H
