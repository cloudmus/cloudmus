#pragma once

#include <QObject>
#include <QVector>

#include "Coro.h"
#include "PlaybackController.h"
#include "PlaylistContext.h"
#include "Settings.h"

namespace App {
class PlaylistEditing;
}
namespace Covers {
class CoverArtCache;
}
namespace History {
class PlaybackHistory;
}
namespace Library {
class TrackStates;
}
namespace Rpc {
class SourceManager;
}

namespace ViewModel {

class Messages;
class Sources;

// The playlist the main area has active — what it shows, and what the
// queue came from: making one active (and playing it, or starting its
// radio), its tracks until they're queued, marking it in the sidebar, and
// bringing the last one back at startup (without playing it).
class ActivePlaylist : public QObject {
    Q_OBJECT

public:
    ActivePlaylist(Playback::PlaybackController& playback, Rpc::SourceManager& sourceManager,
        Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, History::PlaybackHistory& history,
        Config::Settings& settings, Sources& sources, App::PlaylistEditing& playlistEditing, Messages& messages,
        QObject* parent = nullptr);

    const PlaylistContext& context() const { return context_; }
    // History, as a context.
    PlaylistContext historyContext() const;
    // What the main list shows: the queue once there is one, else the
    // active playlist's tracks.
    QVector<Playback::QueueEntry> entries() const;
    // Its tracks are being fetched (the list's busy bar).
    bool isLoading() const { return loading_; }
    // A radio is starting (the hero's Play button spins).
    bool isStartingRadio() const { return startingRadio_; }

    // Makes `context` active without touching playback.
    void setContext(const PlaylistContext& context);
    // Makes `context` active and plays `entries` from `startIndex` (History
    // fills its own; a radio station starts instead).
    void activate(const PlaylistContext& context, const QVector<Playback::QueueEntry>& entries, int startIndex);
    // Fetches the playlist's tracks, then activate()s it.
    Rpc::Task<void> activateAndPlay(QString sourceId, Playlist playlist);
    // Starts a radio from `seed` and, once it runs, makes `context` active.
    Rpc::Task<void> startRadio(QString sourceId, QString seed, PlaylistContext context);
    // The hero's Play: (re)starts the active playlist.
    void play();
    // A row of the main list played.
    void playRow(int row);

signals:
    void contextChanged();
    void entriesChanged();
    void loadingChanged(bool loading);
    void startingRadioChanged(bool starting);
    // Something started playing from here — the sheet steps aside.
    void activated();

private:
    Rpc::Task<void> loadTracks(PlaylistContext context);
    void restoreFrom(const QString& sourceId, const QList<Playlist>& playlists);
    void applyPlaylistEdit(
        const QString& sourceId, const Track& track, const QString& playlistId, bool added, int trackCount);

    Playback::PlaybackController& playback_;
    Rpc::SourceManager& sourceManager_;
    Library::TrackStates& trackStates_;
    Covers::CoverArtCache& coverArtCache_;
    History::PlaybackHistory& history_;
    Config::Settings& settings_;
    Sources& sources_;
    Messages& messages_;

    PlaylistContext context_;
    QVector<Playback::QueueEntry> tracks_;
    bool loading_ = false;
    bool startingRadio_ = false;
    // The active playlist saved last run, until its source lists it.
    Config::Settings::ActivePlaylistRef pendingRestore_;
};

} // namespace ViewModel
