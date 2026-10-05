#pragma once

#include <deque>
#include <optional>

#include "LevelFeed.h"

namespace Playback {

// Turns the analysis frames of what plays (LevelFeed) into what an
// animation wants: a smooth 0..1 level, and a "beat" on each kick drum.
//
// The beat comes from the bass alone: in a finished (compressed) mix the
// full band barely moves on a kick — vocals and synths fill it — while the
// bass jumps. A beat is a jump in bass energy well above how much it has
// been jumping lately (so a busy bassline doesn't fire it) and above how
// loud the bass has been (so small wobbles don't either). Once the kicks
// settle into a tempo, a kick that should be there but wasn't heard (lost
// under the bassline, split across two frames) still gets its beat, on
// the grid — at most two in a row. The thresholds were tuned on real
// tracks: dance music lands ~90% of beats on the tempo grid, rock fires
// less often but still on the beat.
class BeatDetector {
public:
    struct Result {
        float level = 0; // 0..1, quick to rise and slow to fall
        std::optional<float> beat; // strength 0..1, only on the frame of a kick
    };

    Result feed(const LevelReading& reading);
    // Time passed with nothing playing: the level falls away.
    float idle(double dtMs);
    void reset();

    // dB to 0..1: -50 dB (barely audible in music) and below is 0, 0 dB is 1.
    static float normalize(double db);

private:
    struct Recent {
        double pts;
        double flux; // how much the bass energy rose over the frame before
        double energy;
    };

    // A kick was heard at `pts`: the tempo is worked out anew from it.
    void lockTempo(double pts);

    std::deque<Recent> recent_;
    std::deque<double> onsets_; // when kicks were heard, the last few seconds
    double period_ = 0; // seconds per beat; 0 while there's no steady tempo
    double nextBeatPts_ = 0; // where the grid puts the next beat
    int missedInRow_ = 0; // beats the grid supplied since a kick was last heard
    float heardStrength_ = 0.5f; // typical strength of the heard beats
    float level_ = 0;
    double previousPts_ = -1;
    double previousEnergy_ = 0;
    double lastBeatPts_ = -1e9;
};

} // namespace Playback
