#pragma once

#include <QString>
#include <QVector>

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

// A playlist's tracks as queue entries (catalog.listLiked for Liked,
// catalog.listTracks otherwise; not for a radio station), with their
// like state and covers noted on the way. Throws on an RPC failure; empty
// if the source isn't running.
Rpc::Task<QVector<Playback::QueueEntry>> fetchTracks(Rpc::SourceManager& sourceManager,
    Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, QString sourceId, Playlist playlist);

} // namespace App
