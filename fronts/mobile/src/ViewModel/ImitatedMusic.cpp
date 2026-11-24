#include "ImitatedMusic.h"

#include <algorithm>
#include <cmath>

namespace ViewModel {

namespace {
// As mpv's analyzer frames them: 1024 samples at 44.1 kHz.
constexpr double kFrame = 1024.0 / 44100;
// Not more than this at once: after a long gap (nobody asked) the music just
// goes on from now.
constexpr double kMostAtOnce = 0.2;
// A kick in the bass, frame by frame from its start, dB.
constexpr double kKick[] = { -8, -13, -19, -24 };
}

ImitatedMusic::ImitatedMusic(std::function<bool()> playing, quint32 seed)
    : playing_(std::move(playing))
    , random_(seed)
{
    newPhrase();
}

AudioPulse::Source ImitatedMusic::source()
{
    return { [this](bool enabled) {
                if (enabled)
                    clock_.start();
                else
                    clock_.invalidate();
            },
        [this]() {
            if (!clock_.isValid() || !playing_()) {
                if (clock_.isValid())
                    clock_.restart(); // paused: no catching up after
                return QVector<Playback::LevelReading> { };
            }
            return advance(clock_.restart() / 1000.0);
        } };
}

void ImitatedMusic::newPhrase()
{
    // 96..132 BPM, what most music it plays is near; a phrase of 16 beats, and
    // one in five a break with no kick.
    beatSeconds_ = 60.0 / (96 + random_.bounded(37));
    kicksLeft_ = 16;
    breakPhrase_ = random_.bounded(5) == 0;
}

QVector<Playback::LevelReading> ImitatedMusic::advance(double seconds)
{
    QVector<Playback::LevelReading> frames;
    owed_ = std::min(owed_ + seconds, kMostAtOnce);
    while (owed_ >= kFrame) {
        owed_ -= kFrame;
        const double t = pts_;
        pts_ += kFrame;
        // The mix, loud and nearly flat, breathing slowly; the bass under it.
        const double breath = 2.0 * std::sin(t * 2 * M_PI / 12);
        Playback::LevelReading reading { t, -10 + breath + (random_.generateDouble() - 0.5),
            -28 + (random_.generateDouble() - 0.5) * 3 };
        if (t >= nextKick_) {
            lastKick_ = nextKick_;
            if (--kicksLeft_ <= 0)
                newPhrase();
            nextKick_ = lastKick_ + beatSeconds_;
        }
        if (lastKick_ >= 0) {
            const double sinceKick = t - lastKick_;
            const int frameOfKick = int(sinceKick / kFrame);
            if (!breakPhrase_ && frameOfKick < 4)
                reading.bassDb = std::max(reading.bassDb, kKick[frameOfKick]);
            // The hi-hat between the kicks, which only the full band hears.
            if (std::abs(sinceKick - (nextKick_ - lastKick_) / 2) < kFrame / 2)
                reading.fullDb += 4;
        }
        frames.append(reading);
    }
    return frames;
}

} // namespace ViewModel
