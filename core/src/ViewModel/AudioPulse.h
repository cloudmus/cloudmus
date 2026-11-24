#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVector>

#include <functional>

#include "BeatDetector.h"
#include "LevelFeed.h"

namespace Playback {
class PlaybackController;
}

namespace ViewModel {

// How loud the music is right now and when the kick drum hits, for views
// that move with it (the About dialog's clouds). Holding it (acquire()/
// release()) is what turns the whole pipeline on — mpv's analyzer filter,
// its log stream, the polling timer and the detector; with no holder none
// of it runs, so a closed dialog costs nothing.
class AudioPulse : public QObject {
    Q_OBJECT

public:
    // Where the readings come from: switched on and off with the holders,
    // and asked each tick for the ones the speakers have got to.
    struct Source {
        std::function<void(bool enabled)> setEnabled;
        std::function<QVector<Playback::LevelReading>()> take;
    };

    explicit AudioPulse(Playback::PlaybackController& playback, QObject* parent = nullptr);
    explicit AudioPulse(Source source, QObject* parent = nullptr);

    void acquire();
    void release();
    bool isActive() const { return holders_ > 0; }

    // 0..1, smooth; falls to 0 while nothing plays.
    float level() const { return level_; }

    // One tick, `dtMs` after the last. Driven by the timer; public so a
    // test can step time itself.
    void poll(double dtMs);

signals:
    void levelChanged(float level);
    // A kick in the music, on the frame the speakers play it; `strength` 0..1.
    void beat(float strength);

private:
    Source source_;
    Playback::BeatDetector detector_;
    QTimer timer_;
    QElapsedTimer clock_;
    int holders_ = 0;
    float level_ = 0;
    double msSinceReading_ = 0;
};

} // namespace ViewModel
