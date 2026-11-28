#pragma once

#include <QObject>
#include <QString>

struct mpv_handle;

namespace Playback {

// A short sound: what a key press did, for when the window can't be seen (a
// game in full screen hides the notification too); or the app's sound logo,
// played when its logo is tapped in About (ViewModel::LogoSound).
enum class Cue {
    Like,
    Unlike,
    Dislike,
    Undislike,
    Download,
    Logo,
};

// Plays the cues on an mpv of its own, so a cue never touches the track
// that is playing.
class CuePlayer : public QObject {
    Q_OBJECT

public:
    explicit CuePlayer(QObject* parent = nullptr);
    ~CuePlayer() override;

    // A cue cuts off one still playing.
    void play(Cue cue);

    // Where in the cue playing the speakers are, in seconds of the file
    // (mpv's audio-pts: past the output's latency); -1 before it sounds.
    double position() const;

signals:
    // The cue playing ended, or failed to start.
    void finished();

private:
    bool ensureMpv();
    void drainEvents();
    // Where mpv can read the cue from: it can't read Qt resources, so they
    // are copied out on first use. Empty if that failed.
    QString filePath(Cue cue);

    mpv_handle* mpv_ = nullptr;
    // mpv failed to start once: no point trying on every key press.
    bool mpvFailed_ = false;
};

} // namespace Playback
