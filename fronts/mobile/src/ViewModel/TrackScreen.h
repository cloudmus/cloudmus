#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

#include <functional>

#include "Coro.h"
#include "Models.h"
#include "RowsModel.h"

namespace App {
class Core;
}

namespace ViewModel {

class PlaylistScreen;

// The details of one track, from any list, and what can be done with it: a
// sheet's worth of plain properties and slots over TrackActions, NowPlaying,
// and what Library::TrackStates knows. The track is picked first (show…()),
// then the sheet opens on what these say.
class TrackScreen : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY artistChanged)
    Q_PROPERTY(QString album READ album NOTIFY albumChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationMsChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY coverUrlChanged)
    Q_PROPERTY(QString seed READ seed NOTIFY seedChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceNameChanged)
    Q_PROPERTY(QString quality READ quality NOTIFY qualityChanged)
    Q_PROPERTY(QDateTime lastPlayedAt READ lastPlayedAt NOTIFY lastPlayedAtChanged)
    Q_PROPERTY(bool isExplicit READ isExplicit NOTIFY isExplicitChanged)
    Q_PROPERTY(QString webUrl READ webUrl NOTIFY webUrlChanged)
    Q_PROPERTY(bool liked READ liked NOTIFY likedChanged)
    Q_PROPERTY(bool disliked READ disliked NOTIFY dislikedChanged)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY canPlayChanged)
    Q_PROPERTY(bool canLike READ canLike NOTIFY canLikeChanged)
    Q_PROPERTY(bool canDislike READ canDislike NOTIFY canDislikeChanged)
    Q_PROPERTY(bool canStartRadio READ canStartRadio NOTIFY canStartRadioChanged)
    Q_PROPERTY(bool canDownload READ canDownload NOTIFY canDownloadChanged)
    Q_PROPERTY(bool canEditPlaylists READ canEditPlaylists NOTIFY canEditPlaylistsChanged)
    // loadPlaylists() under way; it failed.
    Q_PROPERTY(bool playlistsLoading READ playlistsLoading NOTIFY playlistsLoadingChanged)
    Q_PROPERTY(bool playlistsFailed READ playlistsFailed NOTIFY playlistsFailedChanged)

public:
    TrackScreen(App::Core& core, PlaylistScreen& playlist, QObject* parent = nullptr);

    RowsModel& playlistRows() { return playlistRows_; }

    QString title() const { return track_.title; }
    QString artist() const;
    QString album() const { return track_.album ? track_.album->title : QString(); }
    qint64 durationMs() const { return track_.durationMs; }
    QString coverUrl() const;
    QString seed() const;
    QString sourceName() const;
    QString quality() const;
    QDateTime lastPlayedAt() const;
    bool isExplicit() const { return track_.explicit_.value_or(false); }
    QString webUrl() const { return track_.webUrl.value_or(QString()); }
    bool liked() const;
    bool disliked() const;
    bool canPlay() const { return bool(play_); }
    bool canLike() const;
    bool canDislike() const;
    bool canStartRadio() const;
    bool canDownload() const;
    bool canEditPlaylists() const;
    bool playlistsLoading() const { return playlistsLoading_; }
    bool playlistsFailed() const { return playlistsFailed_; }

public slots:
    // A row of the main list, of the Playlist screen, or what plays.
    void showActiveRow(int row);
    void showOpenRow(int filteredRow);
    void showPlaying();

    void play();
    void playNext();
    void addToQueue();
    void toggleLiked();
    void toggleDisliked();
    // `title`: how the station is shown ("Radio: …"), in the view's language.
    void startRadio(const QString& title);
    void download();
    void loadPlaylists();
    void setInPlaylist(const QString& playlistId, bool in);

signals:
    void titleChanged();
    void artistChanged();
    void albumChanged();
    void durationMsChanged();
    void coverUrlChanged();
    void seedChanged();
    void sourceNameChanged();
    void qualityChanged();
    void lastPlayedAtChanged();
    void isExplicitChanged();
    void webUrlChanged();
    void likedChanged();
    void dislikedChanged();
    void canPlayChanged();
    void canLikeChanged();
    void canDislikeChanged();
    void canStartRadioChanged();
    void canDownloadChanged();
    void canEditPlaylistsChanged();
    void playlistsLoadingChanged();
    void playlistsFailedChanged();

private:
    void show(const QString& sourceId, const Track& track, std::function<void()> play);
    void announce();
    void announceState();
    Rpc::Task<void> fetchPlaylists(QString sourceId, Track track);
    Rpc::Task<void> changePlaylist(QString sourceId, Track track, Playlist playlist, bool in);

    App::Core& core_;
    PlaylistScreen& playlist_;
    RowsModel playlistRows_;
    QString sourceId_;
    Track track_;
    std::function<void()> play_;
    QList<Playlist> playlists_;
    QStringList containing_;
    QStringList changing_;
    bool playlistsLoading_ = false;
    bool playlistsFailed_ = false;
    void showPlaylists();
};

} // namespace ViewModel
