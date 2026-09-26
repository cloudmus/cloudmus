#include "PlaylistEditing.h"

#include <QJsonObject>
#include <QLoggingCategory>

#include "Messages.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "SourceManager.h"

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcPlaylistEditing, "cloudmus.app.playlistediting")
}

PlaylistEditing::PlaylistEditing(Rpc::SourceManager& sourceManager, ViewModel::Messages& messages, QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , messages_(messages)
{
}

bool PlaylistEditing::canEdit(const QString& sourceId) const
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    return client != nullptr
        && client->capabilities()
               .value(QStringLiteral("browse"))
               .toObject()
               .value(QStringLiteral("editPlaylists"))
               .toBool();
}

void PlaylistEditing::setPlaylists(const QString& sourceId, const QList<Playlist>& playlists)
{
    QList<Playlist> editable;
    for (const Playlist& playlist : playlists) {
        if (playlist.editable.value_or(false))
            editable.append(playlist);
    }
    playlists_.insert(sourceId, editable);
}

Rpc::Task<std::optional<PlaylistEditing::Membership>> PlaylistEditing::membership(QString sourceId, QString trackId)
{
    Membership result;
    result.playlists = editablePlaylists(sourceId);
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (result.playlists.isEmpty() || client == nullptr)
        co_return result;
    try {
        result.containing
            = (co_await Rpc::catalogGetTrackPlaylists(*client, GetTrackPlaylistsParams { trackId })).playlistIds;
    } catch (const std::exception& e) {
        qCWarning(lcPlaylistEditing) << "catalog.getTrackPlaylists failed for" << trackId << ":" << e.what();
        co_return std::nullopt;
    }
    co_return result;
}

Rpc::Task<bool> PlaylistEditing::setTrackInPlaylist(QString sourceId, Track track, Playlist playlist, bool add)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return false;
    try {
        int trackCount = 0;
        if (add)
            trackCount = (co_await Rpc::catalogAddToPlaylist(*client, AddToPlaylistParams { playlist.id, track.id }))
                             .trackCount;
        else
            trackCount
                = (co_await Rpc::catalogRemoveFromPlaylist(*client, RemoveFromPlaylistParams { playlist.id, track.id }))
                      .trackCount;
        for (Playlist& known : playlists_[sourceId]) {
            if (known.id == playlist.id)
                known.trackCount = trackCount;
        }
        emit playlistEdited(sourceId, track, playlist.id, add, trackCount);
        messages_.info(add ? tr("Added to \"%1\"").arg(playlist.title) : tr("Removed from \"%1\"").arg(playlist.title));
        co_return true;
    } catch (const std::exception& e) {
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcPlaylistEditing) << "playlist edit failed for" << playlist.id << ":" << message;
        messages_.error(tr("%1: %2").arg(client->sourceName(), message));
        co_return false;
    }
}

} // namespace App
