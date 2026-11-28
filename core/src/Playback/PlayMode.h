#pragma once

namespace Playback {

// What happens when a track ends (PlaybackController::setRepeatMode()).
enum class RepeatMode {
    Off, // on to the next track; the end of the list stops playback
    All, // the list starts over after its last track (not for a radio)
    One, // the current track plays again
};

} // namespace Playback
