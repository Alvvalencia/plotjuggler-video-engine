#ifndef VIDEOENGINE_TESTS_MOCKVIDEOBACKEND_H
#define VIDEOENGINE_TESTS_MOCKVIDEOBACKEND_H

#include "VideoEngine/IVideoBackend.h"
#include <algorithm>
#include <cstdint>

namespace videoengine::testing {

class MockVideoBackend : public IVideoBackend {
public:
    // IVideoBackend interface
    bool open(const QString& path) override;
    void play() override;
    void pause() override;
    void stop() override;
    bool seek(int64_t us) override;
    bool stepForward() override;
    bool stepBackward() override;
    void connectToSink(QVideoSink* sink) override;

    PlaybackState getState() const override;
    int64_t getDurationUs() const override;
    int64_t getPositionUs() const override;

    // Test configuration
    void setOpenResult(bool result);
    void setDuration(int64_t us);
    void setFrameDuration(int64_t us);

private:
    bool openResult_ = true;
    int64_t duration_ = 10'000'000;    // 10 seconds
    int64_t frameDuration_ = 33'333;   // ~30 fps
    int64_t position_ = 0;
    PlaybackState state_ = PlaybackState::IDLE;
    QVideoSink* sink_ = nullptr;
};

} // namespace videoengine::testing

#endif // VIDEOENGINE_TESTS_MOCKVIDEOBACKEND_H
