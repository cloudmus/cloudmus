#pragma once

#include <QObject>
#include <QString>
#include <QVariantAnimation>

#include <optional>

#include "Coro.h"
#include "Models.h"
#include "PlayMode.h"

namespace Config {
class Settings;
}
namespace History {
class PlaybackHistory;
}
namespace Library {
class TrackStates;
}
namespace Playback {
class PlaybackController;
}
namespace Rpc {
class SourceManager;
}

namespace ViewModel {

class Downloads;
class Messages;

// What's playing and what can be done about it: the track, playback state,
// play modes, volume, and the track's like/dislike/download/playlists
// actions — for every view of it (the transport bar, the hero panel, the
// tray; MPRIS and SMTC talk to Playback::PlaybackController directly). Views read
// it through the getters and the change signals and act only through its
// methods; nothing here knows about widgets.
class NowPlaying : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool queueAvailable READ queueAvailable NOTIFY queueAvailabilityChanged)
    Q_PROPERTY(QString webUrl READ webUrl NOTIFY trackChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY playModesChanged)
    Q_PROPERTY(bool radio READ isRadio NOTIFY playModesChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool quiet READ quiet WRITE setQuiet NOTIFY quietChanged)

public:
    // How long the volume glides between normal and quiet. A full second: a
    // shorter fade was a jolt to the ears, not a fade. The volume slider
    // follows the same course.
    static constexpr int fadeMs = 1000;

    NowPlaying(Playback::PlaybackController& playback, Rpc::SourceManager& sourceManager,
        Library::TrackStates& trackStates, History::PlaybackHistory& history, Config::Settings& settings,
        Downloads& downloads, Messages& messages, QObject* parent = nullptr);

    // --- the track
    bool hasTrack() const;
    // The saved track shown before playback starts.
    void setPreviewTrack(const QString& sourceId, const QString& trackId);
    // Only meaningful while hasTrack().
    const Track& track() const;
    QString sourceId() const;
    QString webUrl() const;

    // --- playback
    bool playing() const;
    bool loading() const { return loading_; }
    bool queueAvailable() const;
    void togglePause();
    void next();
    void previous();
    void stop();
    void seek(qint64 positionMs);
    // The level of the mode in effect: the quiet one while quiet(), else the
    // normal one. Quiet mode ducks the sound without pausing or muting, and
    // the playback volume glides between the two levels.
    int volume() const { return quiet_ ? quietVolume_ : volume_; }
    void setVolume(int volume0To100);
    bool quiet() const { return quiet_; }
    void setQuiet(bool on);

    // --- play modes, as in effect for the current queue (a radio can't
    // shuffle or repeat its whole list — see PlaybackController)
    bool shuffle() const;
    Playback::RepeatMode repeatMode() const;
    bool isRadio() const;
    void setShuffle(bool on);
    void setRepeatMode(Playback::RepeatMode mode);

    // --- what can be done about the track. `…Supported` is what its
    // source declares; `…Busy` is a request in flight — while it is,
    // liked/disliked already show the state asked for.
    struct Feedback {
        bool likeSupported = false;
        bool liked = false;
        bool likeBusy = false;
        bool dislikeSupported = false;
        bool disliked = false;
        bool dislikeBusy = false;
        bool downloadSupported = false;
        bool downloadBusy = false;
        bool playlistsSupported = false;
    };
    Feedback feedback() const;
    void setLiked(bool liked);
    void setDisliked(bool disliked);
    void download();

    // The same for any track (the track lists' context menus).
    // `announce`: tell the user once it's done — the toolbar's own button
    // already shows it. (Downloading any track: ViewModel::Downloads.)
    Rpc::Task<void> setTrackLiked(QString sourceId, QString trackId, bool liked, bool announce);
    Rpc::Task<void> setTrackDisliked(QString sourceId, QString trackId, bool disliked, bool announce);

signals:
    // A new track started, or the current one went away (hasTrack() false).
    void trackChanged();
    void playingChanged(bool playing);
    void loadingChanged(bool loading);
    void positionChanged(qint64 positionMs, qint64 durationMs);
    void bufferedChanged(qint64 bufferedMs); // -1: not cached (a local file)
    void queueAvailabilityChanged(bool available);
    void playModesChanged();
    // The level shown for the mode in effect — also emitted when quiet mode
    // switches it.
    void volumeChanged(int volume0To100);
    void quietChanged(bool quiet);
    void feedbackChanged();

private:
    bool isCurrent(const QString& sourceId, const QString& trackId) const;
    bool isShownTrack(const QString& sourceId, const QString& trackId) const;
    QJsonObject capabilities(const QString& sourceId) const;
    void resetBusy();
    void applyVolume(bool animated);

    Playback::PlaybackController& playback_;
    Rpc::SourceManager& sourceManager_;
    Library::TrackStates& trackStates_;
    History::PlaybackHistory& history_;
    Config::Settings& settings_;
    Downloads& downloads_;
    Messages& messages_;

    bool loading_ = false;
    int volume_ = 100;
    int quietVolume_ = 30;
    bool quiet_ = false;
    // Drives the volume mpv gets, so switching modes fades instead of jumping.
    QVariantAnimation fade_;
    // In-flight requests for the current track, with the state asked for.
    std::optional<bool> pendingLiked_;
    std::optional<bool> pendingDisliked_;
    QString previewSourceId_;
    QString previewTrackId_;
};

} // namespace ViewModel
