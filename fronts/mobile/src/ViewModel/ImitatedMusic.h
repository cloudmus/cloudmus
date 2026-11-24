#pragma once

#include <QElapsedTimer>
#include <QRandomGenerator>

#include <functional>

#include "AudioPulse.h"

namespace ViewModel {

// Readings of music made up, for an AudioPulse where the real ones can't be
// had: Android's libmpv has no filters to analyze what plays (docs/android.md,
// "a libmpv of our own"). A steady kick at a tempo of its own, now and then a
// break, the loudness breathing over a phrase, all while `playing` says
// something plays; the same BeatDetector makes beats of them as of real music,
// so the views look alive. Goes once the real readings are there.
class ImitatedMusic {
public:
    explicit ImitatedMusic(std::function<bool()> playing, quint32 seed = QRandomGenerator::global()->generate());

    // For AudioPulse(Source).
    AudioPulse::Source source();

    // The readings for `seconds` more of music: public for a test to step time.
    QVector<Playback::LevelReading> advance(double seconds);

private:
    void newPhrase();

    std::function<bool()> playing_;
    QRandomGenerator random_;
    QElapsedTimer clock_;
    double pts_ = 0; // seconds of music made up so far
    double owed_ = 0; // of a frame, carried to the next call
    double beatSeconds_ = 0.5;
    double lastKick_ = -1;
    double nextKick_ = 0;
    int kicksLeft_ = 0; // in this phrase
    bool breakPhrase_ = false;
};

} // namespace ViewModel
