#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>

namespace Playback {
class CuePlayer;
}

namespace ViewModel {

// The app's sound logo, and the pulse that goes with it: tapped in About, the
// logo plays it (resources/sounds/cloudmus-logo.mp3) and swells hard on each of
// its strikes, the clouds rushing with them, and glows with its loudness. The
// strikes and the loudness are the sound's own, found in it once (see the
// tables in the .cpp) rather than analyzed live: the Android build's mpv can't
// analyze (docs/plans/mpv.md). Kept in time with what's heard by where in it
// the speakers are, not by a clock started at the tap.
class LogoSound : public QObject {
    Q_OBJECT

public:
    // What plays the sound: starts it, and says how far in the speakers are
    // (seconds of the file, -1 before it sounds). A test's own stands in.
    struct Player {
        std::function<void()> play;
        std::function<double()> position;
    };

    // With a player of its own (Playback::CuePlayer, an mpv that starts on
    // the first play()).
    explicit LogoSound(QObject* parent = nullptr);
    explicit LogoSound(Player player, QObject* parent = nullptr);
    ~LogoSound() override;

    // From the start, again if it's playing. Over music, only once that has
    // stepped back (the duck, on playingChanged): the two don't run together.
    void play();
    // Whether music plays, for the wait above. Unset: never waited for.
    void setMusicPlaying(std::function<bool()> musicPlaying) { musicPlaying_ = std::move(musicPlaying); }
    bool playing() const { return playing_; }
    // 0..1, how loud it is now; 0 while it doesn't play.
    float level() const { return level_; }

    // One step, the speakers `position` seconds into the sound (-1: not
    // sounding yet). Driven by the timer; public for a test to step time.
    void step(double position);
    // Ended: the player's.
    void stop();

signals:
    // A strike in it, as it's heard; `strength` 0..1, the strongest 1.
    void hit(float strength);
    void levelChanged(float level);
    void playingChanged(bool playing);

private:
    void setLevel(float level);
    void sound();

    std::unique_ptr<Playback::CuePlayer> cues_;
    Player player_;
    QTimer timer_;
    QTimer leadIn_;
    std::function<bool()> musicPlaying_;
    QElapsedTimer sincePlay_;
    int nextHit_ = 0;
    float level_ = 0;
    bool playing_ = false;
};

} // namespace ViewModel
