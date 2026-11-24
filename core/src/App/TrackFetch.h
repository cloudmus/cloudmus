#pragma once

#include <QString>
#include <QVector>

#include <optional>

#include "Coro.h"
#include "Models.h"
#include "PlaybackController.h"

namespace Covers {
class CoverArtCache;
}
namespace Library {
class TrackStates;
}
namespace Rpc {
class SourceManager;
}

namespace App {

// What each playlist listed last, on disk: for a playlist to open and play
// with no network (the downloaded tracks), and at once while its source is
// still starting. One file per (source, playlist); the oldest go once there
// are more than kMaxFiles of them.
class PlaylistTrackCache {
public:
    static constexpr int kMaxFiles = 80;

    explicit PlaylistTrackCache(QString dir);

    void save(const QString& sourceId, const QString& playlistId, const QVector<Playback::QueueEntry>& entries) const;
    // Empty (nullopt) if there is no usable copy.
    std::optional<QVector<Playback::QueueEntry>> load(const QString& sourceId, const QString& playlistId) const;
    // Signed out: it isn't the user's any more.
    void removeSource(const QString& sourceId) const;

private:
    QString fileFor(const QString& sourceId, const QString& playlistId) const;

    QString dir_;
};

// A playlist's tracks as queue entries (catalog.listLiked for Liked,
// catalog.listTracks otherwise; not for a radio station), with their
// like state and covers noted on the way. Throws on an RPC failure; empty
// if the source isn't running. With `cache`, what it got is saved there.
Rpc::Task<QVector<Playback::QueueEntry>> fetchTracks(Rpc::SourceManager& sourceManager,
    Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, QString sourceId, Playlist playlist,
    const PlaylistTrackCache* cache = nullptr);

// What `cache` has of the playlist, noted like a fetch's tracks (like state,
// covers); nullopt if there is nothing.
std::optional<QVector<Playback::QueueEntry>> cachedTracks(const PlaylistTrackCache& cache,
    Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, const QString& sourceId,
    const Playlist& playlist);

} // namespace App
