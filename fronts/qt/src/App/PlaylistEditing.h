#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

#include "Coro.h"
#include "Models.h"

namespace Rpc {
class SourceManager;
}
namespace ViewModel {
class Messages;
}

namespace App {

// Adding a track to, and removing it from, the user's own playlists
// (docs/protocol.md §7.6) — for every "Add to playlist" menu: the track
// lists', the toolbar's, the tray's. Knows each source's editable
// playlists from what it lists (setPlaylists()), asks the source which of
// them hold a track, makes the change, and tells everyone showing those
// playlists (playlistEdited()).
class PlaylistEditing : public QObject {
    Q_OBJECT

public:
    PlaylistEditing(Rpc::SourceManager& sourceManager, ViewModel::Messages& messages, QObject* parent = nullptr);

    // Whether the source edits playlists at all (browse.editPlaylists).
    bool canEdit(const QString& sourceId) const;

    // What a source lists (catalog.listPlaylists), as it's (re)loaded —
    // only the editable ones are kept.
    void setPlaylists(const QString& sourceId, const QList<Playlist>& playlists);
    QList<Playlist> editablePlaylists(const QString& sourceId) const { return playlists_.value(sourceId); }

    // The source's editable playlists and which of them hold the track
    // (catalog.getTrackPlaylists); nullopt if that couldn't be found out.
    struct Membership {
        QList<Playlist> playlists;
        QStringList containing;
    };
    Rpc::Task<std::optional<Membership>> membership(QString sourceId, QString trackId);

    // Adds or removes the track, tells the user how it went, and on
    // success emits playlistEdited(). False if it failed.
    Rpc::Task<bool> setTrackInPlaylist(QString sourceId, Track track, Playlist playlist, bool add);

signals:
    // A track was added to / removed from a playlist, which now holds
    // `trackCount` tracks.
    void playlistEdited(
        const QString& sourceId, const Track& track, const QString& playlistId, bool added, int trackCount);

private:
    Rpc::SourceManager& sourceManager_;
    ViewModel::Messages& messages_;
    QHash<QString, QList<Playlist>> playlists_;
};

} // namespace App
