#pragma once

#include <QObject>
#include <QString>

#include "Models.h"
#include "PlaylistContext.h"

namespace App {
class PlaylistEditing;
}
namespace Playback {
class PlaybackController;
}
namespace Rpc {
class SourceManager;
}

namespace ViewModel {

class ActivePlaylist;
class Downloads;

// What can be done with any track, wherever it's listed: for every view's menu
// of it. Which actions a track has follows from its source's capabilities.
class TrackActions : public QObject {
    Q_OBJECT

public:
    struct Available {
        bool like = false;
        bool dislike = false;
        bool radio = false;
        bool download = false;
        bool playlists = false;
    };

    TrackActions(Rpc::SourceManager& sourceManager, Playback::PlaybackController& playback,
        ActivePlaylist& activePlaylist, Downloads& downloads, App::PlaylistEditing& playlistEditing,
        QObject* parent = nullptr);

    Available available(const QString& sourceId) const;
    // Whether a whole playlist can be saved: not a station, downloads on.
    bool canDownloadPlaylist(const QString& sourceId, const Playlist& playlist) const;

    void playNext(const QString& sourceId, const Track& track);
    void addToQueue(const QString& sourceId, const Track& track);
    // A station from this track, shown as `title`. Not in the source's
    // playlists, but saved with its tracks like any active playlist, so the
    // next run restores it.
    void startRadio(const QString& sourceId, const Track& track, const QString& title);

private:
    Rpc::SourceManager& sourceManager_;
    Playback::PlaybackController& playback_;
    ActivePlaylist& activePlaylist_;
    Downloads& downloads_;
    App::PlaylistEditing& playlistEditing_;
};

} // namespace ViewModel
