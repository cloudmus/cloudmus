#include "TrackActions.h"

#include <QJsonObject>

#include "ActivePlaylist.h"
#include "Downloads.h"
#include "PlaybackController.h"
#include "PlaylistEditing.h"
#include "RpcClient.h"
#include "SourceManager.h"

namespace ViewModel {

TrackActions::TrackActions(Rpc::SourceManager& sourceManager, Playback::PlaybackController& playback,
    ActivePlaylist& activePlaylist, Downloads& downloads, App::PlaylistEditing& playlistEditing, QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , playback_(playback)
    , activePlaylist_(activePlaylist)
    , downloads_(downloads)
    , playlistEditing_(playlistEditing)
{
}

TrackActions::Available TrackActions::available(const QString& sourceId) const
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    const QJsonObject capabilities = client != nullptr ? client->capabilities() : QJsonObject();
    const QJsonObject feedback = capabilities.value(QStringLiteral("feedback")).toObject();
    Available result;
    result.like = feedback.value(QStringLiteral("like")).toBool();
    result.dislike = feedback.value(QStringLiteral("dislike")).toBool();
    result.radio = capabilities.value(QStringLiteral("browse")).toObject().value(QStringLiteral("radio")).toBool();
    // Only while downloads are on (Settings → Downloads).
    result.download = downloads_.isEnabled() && Downloads::canSave(capabilities);
    result.playlists = playlistEditing_.canEdit(sourceId);
    return result;
}

bool TrackActions::canDownloadPlaylist(const QString& sourceId, const Playlist& playlist) const
{
    if (!downloads_.isEnabled() || sourceId.isEmpty() || playlist.kind == QStringLiteral("radioStation"))
        return false;
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    return client != nullptr && Downloads::canSave(client->capabilities());
}

void TrackActions::playNext(const QString& sourceId, const Track& track) { playback_.enqueueNext(sourceId, track); }

void TrackActions::addToQueue(const QString& sourceId, const Track& track) { playback_.enqueueAtEnd(sourceId, track); }

void TrackActions::startRadio(const QString& sourceId, const Track& track, const QString& title)
{
    // The bare track id — each backend that declares browse.radio decides
    // for itself how to turn a track id into whatever station-addressing
    // scheme it needs (e.g. Yandex's rotor API wants a "track:<id>"-style
    // address; YouTube's get_watch_playlist takes a plain videoId directly).
    // A front-imposed prefix here broke YouTube radio (it received
    // "track:<id>" as a literal, invalid videoId) — seed is source-defined
    // per docs/protocol.md §7.1, so the front must not format it.
    const PlaylistContext context { sourceId,
        Playlist { track.id, title, std::nullopt, std::nullopt, 0, QStringLiteral("radioStation") } };
    activePlaylist_.startRadio(sourceId, track.id, context).detach();
}

} // namespace ViewModel
