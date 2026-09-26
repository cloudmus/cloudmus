#include "TrackFetch.h"

#include "CoverArtCache.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "SourceManager.h"
#include "TrackStates.h"

namespace App {

Rpc::Task<QVector<Playback::QueueEntry>> fetchTracks(Rpc::SourceManager& sourceManager,
    Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, QString sourceId, Playlist playlist)
{
    QVector<Playback::QueueEntry> entries;
    Rpc::RpcClient* client = sourceManager.client(sourceId);
    if (client == nullptr)
        co_return entries;
    QList<Track> tracks;
    if (playlist.kind == QStringLiteral("liked")) {
        ListLikedParams params { std::nullopt };
        tracks = (co_await Rpc::catalogListLiked(*client, params)).tracks;
    } else {
        ListTracksParams params { playlist.id, std::nullopt };
        tracks = (co_await Rpc::catalogListTracks(*client, params)).tracks;
    }
    trackStates.observe(sourceId, tracks);
    coverArtCache.assignSource(sourceId, tracks);
    entries.reserve(tracks.size());
    for (const Track& track : tracks)
        entries.append(Playback::QueueEntry { sourceId, track });
    co_return entries;
}

} // namespace App
