#include "ActivePlaylist.h"

#include <QLoggingCategory>

#include "CoverArtCache.h"
#include "Messages.h"
#include "PlaybackHistory.h"
#include "PlaylistEditing.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "SourceManager.h"
#include "Sources.h"
#include "TrackFetch.h"
#include "TrackStates.h"

namespace ViewModel {

namespace {
Q_LOGGING_CATEGORY(lcActivePlaylist, "cloudmus.viewmodel.activeplaylist")
}

ActivePlaylist::ActivePlaylist(Playback::PlaybackController& playback, Rpc::SourceManager& sourceManager,
    Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, History::PlaybackHistory& history,
    Config::Settings& settings, Sources& sources, App::PlaylistEditing& playlistEditing, Messages& messages,
    QObject* parent)
    : QObject(parent)
    , playback_(playback)
    , sourceManager_(sourceManager)
    , trackStates_(trackStates)
    , coverArtCache_(coverArtCache)
    , history_(history)
    , settings_(settings)
    , sources_(sources)
    , messages_(messages)
{
    connect(&playback_, &Playback::PlaybackController::queueChanged, this, &ActivePlaylist::entriesChanged);
    connect(&playlistEditing, &App::PlaylistEditing::playlistEdited, this, &ActivePlaylist::applyPlaylistEdit);

    // Restore the last active playlist (without playing it). History is
    // local, so it's restored right away; a source's playlist has to wait
    // for that source to list its playlists.
    const Config::Settings::ActivePlaylistRef saved = settings_.lastActivePlaylist();
    if (saved.kind == QStringLiteral("history")) {
        setContext(historyContext());
        loadTracks(context_).detach();
    } else if (!saved.sourceId.isEmpty() && !saved.playlistId.isEmpty()) {
        pendingRestore_ = saved;
        connect(&sources_, &Sources::playlistsLoaded, this, &ActivePlaylist::restoreFrom);
    }
}

PlaylistContext ActivePlaylist::historyContext() const
{
    PlaylistContext context;
    context.isHistory = true;
    context.playlist = Playlist { QStringLiteral("history"), tr("History"), std::nullopt, std::nullopt,
        static_cast<int>(history_.entries().size()), QStringLiteral("playlist") };
    return context;
}

QVector<Playback::QueueEntry> ActivePlaylist::entries() const
{
    return playback_.hasQueue() ? playback_.queue() : tracks_;
}

void ActivePlaylist::setContext(const PlaylistContext& context)
{
    const bool changed = !context_.sameAs(context);
    context_ = context;
    if (changed)
        tracks_.clear();
    sources_.model().setActivePlaylist(context.isHistory ? QString() : context.sourceId,
        context.isHistory ? QStringLiteral("history") : context.playlist.id);
    if (context.persistent) {
        settings_.setLastActivePlaylist({ context.isHistory ? QString() : context.sourceId, context.playlist.id,
            context.isHistory ? QStringLiteral("history") : context.playlist.kind });
    }
    // Something the user made active wins over what's still to restore.
    pendingRestore_ = Config::Settings::ActivePlaylistRef();
    emit contextChanged();
    emit entriesChanged();
}

void ActivePlaylist::activate(
    const PlaylistContext& context, const QVector<Playback::QueueEntry>& entries, int startIndex)
{
    if (context.isRadio()) {
        startRadio(context.sourceId, context.playlist.id, context).detach();
        emit activated();
        return;
    }
    QVector<Playback::QueueEntry> queue = entries;
    if (queue.isEmpty() && context.isHistory) {
        for (const History::HistoryEntry& e : history_.entries())
            queue.append(Playback::QueueEntry { e.sourceId, e.track });
    }
    if (queue.isEmpty())
        return;
    setContext(context);
    playback_.loadQueue(queue, qBound(0, startIndex, int(queue.size()) - 1));
    emit activated();
}

Rpc::Task<void> ActivePlaylist::activateAndPlay(QString sourceId, Playlist playlist)
{
    const PlaylistContext context { sourceId, playlist };
    if (context.isRadio()) {
        activate(context, { }, 0);
        co_return;
    }
    try {
        const QVector<Playback::QueueEntry> entries
            = co_await App::fetchTracks(sourceManager_, trackStates_, coverArtCache_, sourceId, playlist);
        activate(context, entries, 0);
    } catch (const std::exception& e) {
        qCWarning(lcActivePlaylist) << "loading tracks failed for" << sourceId << ":" << e.what();
        messages_.error(QString::fromStdString(e.what()));
    }
}

Rpc::Task<void> ActivePlaylist::startRadio(QString sourceId, QString seed, PlaylistContext context)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    startingRadio_ = true;
    emit startingRadioChanged(true);
    try {
        StartRadioParams params { seed };
        StartRadioResult result = co_await Rpc::catalogStartRadio(*client, params);
        trackStates_.observe(sourceId, result.initialTracks);
        coverArtCache_.assignSource(sourceId, result.initialTracks);
        // Before startRadio(), so the queue it emits lands in the main
        // list under the right playlist.
        setContext(context);
        playback_.startRadio(sourceId, result.stationId, result.initialTracks);
    } catch (const std::exception& e) {
        qCWarning(lcActivePlaylist) << "starting radio failed for" << sourceId << ":" << e.what();
        messages_.error(QString::fromStdString(e.what()));
    }
    startingRadio_ = false;
    emit startingRadioChanged(false);
}

void ActivePlaylist::play()
{
    if (!context_.isValid())
        return;
    if (context_.isRadio())
        startRadio(context_.sourceId, context_.playlist.id, context_).detach();
    else if (playback_.hasQueue())
        playback_.playAt(0);
    else
        activate(context_, tracks_, 0);
}

void ActivePlaylist::playRow(int row)
{
    // The main list mirrors the queue once there is one (see entries()),
    // so its rows are queue indices.
    if (playback_.hasQueue())
        playback_.playAt(row);
    else
        activate(context_, tracks_, row);
}

Rpc::Task<void> ActivePlaylist::loadTracks(PlaylistContext context)
{
    if (context.isRadio())
        co_return;
    QVector<Playback::QueueEntry> entries;
    if (context.isHistory) {
        for (const History::HistoryEntry& e : history_.entries())
            entries.append(Playback::QueueEntry { e.sourceId, e.track });
    } else {
        loading_ = true;
        emit loadingChanged(true);
        try {
            entries = co_await App::fetchTracks(
                sourceManager_, trackStates_, coverArtCache_, context.sourceId, context.playlist);
        } catch (const std::exception& e) {
            qCWarning(lcActivePlaylist) << "loading tracks failed for" << context.sourceId << ":" << e.what();
        }
        loading_ = false;
        emit loadingChanged(false);
    }
    // Only if it's still the active playlist.
    if (!context_.sameAs(context))
        co_return;
    tracks_ = entries;
    emit entriesChanged();
}

void ActivePlaylist::restoreFrom(const QString& sourceId, const QList<Playlist>& playlists)
{
    if (pendingRestore_.sourceId != sourceId)
        return;
    const QString playlistId = pendingRestore_.playlistId;
    pendingRestore_ = Config::Settings::ActivePlaylistRef();
    disconnect(&sources_, &Sources::playlistsLoaded, this, &ActivePlaylist::restoreFrom);
    // Unless something else became active in the meantime.
    if (context_.isValid())
        return;
    for (const Playlist& playlist : playlists) {
        if (playlist.id != playlistId)
            continue;
        setContext(PlaylistContext { sourceId, playlist });
        loadTracks(context_).detach();
        break;
    }
}

void ActivePlaylist::applyPlaylistEdit(
    const QString& sourceId, const Track& track, const QString& playlistId, bool added, int trackCount)
{
    if (context_.isHistory || context_.sourceId != sourceId || context_.playlist.id != playlistId)
        return;
    context_.playlist.trackCount = trackCount;
    // Only the not-yet-queued tracks — a running queue stays as it is.
    if (playback_.hasQueue())
        return;
    if (added) {
        tracks_.append(Playback::QueueEntry { sourceId, track });
    } else {
        for (int i = 0; i < tracks_.size(); ++i) {
            if (tracks_[i].track.id == track.id) {
                tracks_.removeAt(i);
                break;
            }
        }
    }
    emit entriesChanged();
}

} // namespace ViewModel
