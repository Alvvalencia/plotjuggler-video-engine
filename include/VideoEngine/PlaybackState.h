#ifndef VIDEOENGINE_PLAYBACKSTATE_H
#define VIDEOENGINE_PLAYBACKSTATE_H

namespace videoengine {

enum class PlaybackState {
    IDLE,
    LOADED,
    PLAYING,
    PAUSED,
    STOPPED,
    ERROR
};

} // namespace videoengine

#endif // VIDEOENGINE_PLAYBACKSTATE_H
