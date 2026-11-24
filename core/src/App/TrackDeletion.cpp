#include "TrackDeletion.h"

#include <QLoggingCategory>

#include "HostMedia.h"
#include "Messages.h"
#include "RpcMethods.h"
#include "SourceManager.h"
#include "Sources.h"

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcTrackDeletion, "cloudmus.app.trackdeletion")
}

TrackDeletion::TrackDeletion(
    Rpc::SourceManager& sourceManager, ViewModel::Sources& sources, ViewModel::Messages& messages, QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , sources_(sources)
    , messages_(messages)
{
}

bool TrackDeletion::supports(const QString& sourceId) const
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    return client != nullptr && client->available()
        && client->capabilities().value(QStringLiteral("browse")).toObject().value(QStringLiteral("delete")).toBool();
}

bool TrackDeletion::canDelete(const QString& sourceId, const Track& track) const
{
    return track.deletable.value_or(false) && supports(sourceId);
}

bool TrackDeletion::canDelete(const QString& sourceId, const Playlist& playlist) const
{
    return playlist.deletable.value_or(false) && supports(sourceId);
}

void TrackDeletion::deleteTracks(const QString& sourceId, const QStringList& trackIds)
{
    if (!trackIds.isEmpty() && supports(sourceId))
        run(sourceId, trackIds, std::nullopt).detach();
}

void TrackDeletion::deletePlaylist(const QString& sourceId, const Playlist& playlist)
{
    if (canDelete(sourceId, playlist))
        run(sourceId, { }, playlist).detach();
}

Rpc::Task<void> TrackDeletion::run(QString sourceId, QStringList trackIds, std::optional<Playlist> playlist)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    const QString sourceName = client->sourceName();
    Deletion result;
    try {
        result = playlist ? co_await Rpc::catalogDeletePlaylist(*client, DeletePlaylistParams { playlist->id })
                          : co_await Rpc::catalogDeleteTracks(*client, DeleteTracksParams { trackIds });
    } catch (const std::exception& e) {
        qCWarning(lcTrackDeletion) << sourceId << "could not delete:" << e.what();
        messages_.failure(sourceName, e);
        co_return;
    }
    QStringList gone = result.deleted;
    // What the platform's library keeps, it deletes; it may ask the user
    // first, and lists the library anew once done.
    if (!result.host.isEmpty()) {
        QStringList uris;
        for (const HostDeletion& item : result.host) {
            uris.append(item.uri);
            gone.append(item.trackId);
        }
        if (Rpc::HostMedia* media = sourceManager_.hostMedia())
            media->remove(uris);
    }
    if (!result.failed.isEmpty()) {
        QStringList reasons;
        for (const FailedDeletion& failure : result.failed)
            reasons.append(failure.message);
        reasons.removeDuplicates();
        messages_.error(tr("%n track(s) couldn't be deleted", nullptr, int(result.failed.size())),
            reasons.join(QLatin1Char('\n')));
    }
    if (!result.deleted.isEmpty())
        messages_.info(playlist ? tr("Deleted \"%1\"").arg(playlist->title)
                                : tr("Deleted %n track(s)", nullptr, int(result.deleted.size())));
    if (!gone.isEmpty())
        emit deleted(sourceId, gone);
    sources_.refresh(sourceId);
}

} // namespace App
