#include "PlaylistEditing.h"

#include <QJsonObject>
#include <QLoggingCategory>

#include "Messages.h"
#include "Outbox.h"
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

void PlaylistEditing::setOutbox(Outbox* outbox)
{
    outbox_ = outbox;
    if (outbox_ == nullptr)
        return;
    // Sent, or given up on: what the source says counts again.
    const auto settled = [this](const Outbox::Item& item) {
        if (!item.key.startsWith(QStringLiteral("playlist:")))
            return;
        auto it = pendingEdits_.find(item.sourceId);
        if (it != pendingEdits_.end())
            it->remove(item.key);
    };
    connect(outbox_, &Outbox::delivered, this, settled);
    connect(outbox_, &Outbox::dropped, this, [settled](const Outbox::Item& item, const QString&) { settled(item); });
}

QString PlaylistEditing::editKey(const QString& playlistId, const QString& trackId)
{
    return QStringLiteral("playlist:%1:%2").arg(playlistId, trackId);
}

void PlaylistEditing::applyPending(const QString& sourceId, const QString& trackId, QStringList& containing) const
{
    const QHash<QString, bool> pending = pendingEdits_.value(sourceId);
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        // playlist:<playlistId>:<trackId>; ids may hold ':' themselves.
        const QString suffix = QLatin1Char(':') + trackId;
        if (!it.key().endsWith(suffix))
            continue;
        const QString playlistId = it.key().mid(9, it.key().size() - 9 - suffix.size());
        containing.removeAll(playlistId);
        if (it.value())
            containing.append(playlistId);
    }
}

Rpc::Task<std::optional<PlaylistEditing::Membership>> PlaylistEditing::membership(QString sourceId, QString trackId)
{
    Membership result;
    result.playlists = editablePlaylists(sourceId);
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (result.playlists.isEmpty())
        co_return result;
    const bool offline = outbox_ != nullptr && outbox_->holds(sourceId);
    if (client != nullptr && !offline) {
        try {
            result.containing
                = (co_await Rpc::catalogGetTrackPlaylists(*client, GetTrackPlaylistsParams { trackId })).playlistIds;
            applyPending(sourceId, trackId, result.containing);
            co_return result;
        } catch (const Rpc::RpcCallException& e) {
            qCWarning(lcPlaylistEditing) << "catalog.getTrackPlaylists failed for" << trackId << ":" << e.what();
            if (!e.isOffline())
                co_return std::nullopt;
        } catch (const std::exception& e) {
            qCWarning(lcPlaylistEditing) << "catalog.getTrackPlaylists failed for" << trackId << ":" << e.what();
            co_return std::nullopt;
        }
    }
    // No network: by what was saved of each playlist.
    if (!savedMembership_)
        co_return std::nullopt;
    result.containing = savedMembership_(sourceId, trackId, result.playlists);
    applyPending(sourceId, trackId, result.containing);
    co_return result;
}

void PlaylistEditing::queueEdit(const QString& sourceId, const Track& track, const Playlist& playlist, bool add)
{
    const QString key = editKey(playlist.id, track.id);
    if (add)
        outbox_->enqueue(sourceId, QStringLiteral("catalog.addToPlaylist"),
            AddToPlaylistParams { playlist.id, track.id }.toJson(), key);
    else
        outbox_->enqueue(sourceId, QStringLiteral("catalog.removeFromPlaylist"),
            RemoveFromPlaylistParams { playlist.id, track.id }.toJson(), key);
    pendingEdits_[sourceId].insert(key, add);
    // The count as it will be: the source's own once it's told.
    int trackCount = qMax(0, playlist.trackCount + (add ? 1 : -1));
    for (Playlist& known : playlists_[sourceId]) {
        if (known.id == playlist.id) {
            trackCount = qMax(0, known.trackCount + (add ? 1 : -1));
            known.trackCount = trackCount;
        }
    }
    emit playlistEdited(sourceId, track, playlist.id, add, trackCount);
    messages_.info(add ? tr("Added to \"%1\"").arg(playlist.title) : tr("Removed from \"%1\"").arg(playlist.title));
}

Rpc::Task<bool> PlaylistEditing::setTrackInPlaylist(QString sourceId, Track track, Playlist playlist, bool add)
{
    // After the edits asked for before it: an add and a remove reach the
    // source in the order they were made.
    const Rpc::Sequence::Turn turn = co_await edits_.turn();
    // The source can't be told now, or an edit before it still waits.
    if (outbox_ != nullptr && outbox_->holds(sourceId)) {
        queueEdit(sourceId, track, playlist, add);
        co_return true;
    }
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
        const auto* call = dynamic_cast<const Rpc::RpcCallException*>(&e);
        if (call != nullptr && call->isOffline() && outbox_ != nullptr) {
            // The network went as it was sent: later, then.
            queueEdit(sourceId, track, playlist, add);
            co_return true;
        }
        messages_.failure(client->sourceName(), e);
        co_return false;
    }
}

} // namespace App
