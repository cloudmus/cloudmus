#pragma once

#include <QObject>
#include <QString>

#include "Throttle.h"

namespace Playback {
class QueuePlayer;
}

namespace ViewModel {

// What's playing and what can be done about it, for every view of it (the
// mini player, the Now Playing page; MediaSession talks to the core through
// CorePlayer). Named and shaped after the desktop ViewModel::NowPlaying so
// views written against this one keep working when that one replaces it.
// Actions are slots: in the Android UI process this object is a Qt Remote
// Objects replica, and a replica only carries properties, signals and slots.
class NowPlaying : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY trackChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY positionChanged)

public:
    explicit NowPlaying(Playback::QueuePlayer& player, QObject* parent = nullptr);

    bool hasTrack() const;
    QString title() const;
    QString artist() const;
    QString coverUrl() const;
    int currentIndex() const;
    bool playing() const;
    bool loading() const;
    qint64 positionMs() const;
    qint64 durationMs() const;

public slots:
    void play();
    void pause();
    void togglePause();
    void next();
    void previous();
    void seek(qint64 positionMs);
    // Playback stops and the player is done with: on Android the playback
    // service and its process end (stopped() tells them).
    void stop();

signals:
    void trackChanged();
    void playingChanged();
    void loadingChanged();
    // At most a few times a second: it crosses a process on Android.
    void positionChanged();
    void stopped();

private:
    Playback::QueuePlayer& player_;
    Timing::Throttle positionThrottle_;
};

} // namespace ViewModel
