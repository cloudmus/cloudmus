#include "OpenPlaylist.h"

#include <QLoggingCategory>

#include "ActivePlaylist.h"
#include "Messages.h"
#include "PlaybackHistory.h"
#include "PlaylistEditing.h"
#include "RpcClient.h"
#include "TrackFetch.h"

namespace ViewModel {

namespace {
Q_LOGGING_CATEGORY(lcOpenPlaylist, "cloudmus.viewmodel.openplaylist")
}

OpenPlaylist::OpenPlaylist(Rpc::SourceManager& sourceManager, Library::TrackStates& trackStates,
    Covers::CoverArtCache& coverArtCache, History::PlaybackHistory& history, ActivePlaylist& activePlaylist,
    App::PlaylistEditing& playlistEditing, Messages& messages, const App::PlaylistTrackCache& playlistTracks,
    QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , trackStates_(trackStates)
    , coverArtCache_(coverArtCache)
    , history_(history)
    , activePlaylist_(activePlaylist)
    , messages_(messages)
    , playlistTracks_(playlistTracks)
{
    connect(&history_, &History::PlaybackHistory::changed, this, [this]() {
        // Refresh in place — a track just started playing.
        if (context_.isHistory)
            fillHistory();
    });
    connect(&playlistEditing, &App::PlaylistEditing::playlistEdited, this, &OpenPlaylist::applyEdit);
}

void OpenPlaylist::open(const QString& sourceId, const Playlist& playlist)
{
    setContext(PlaylistContext { sourceId, playlist });
    load();
}

void OpenPlaylist::openHistory()
{
    setContext(activePlaylist_.historyContext());
    fillHistory();
}

void OpenPlaylist::close()
{
    setContext(PlaylistContext());
    announceRows();
}

void OpenPlaylist::setContext(const PlaylistContext& context)
{
    context_ = context;
    rows_.clear();
    if (loading_) {
        loading_ = false;
        emit loadingChanged(false);
    }
}

void OpenPlaylist::load()
{
    // What it listed last is there at once, with the fresh list to follow.
    if (!context_.isRadio()) {
        const auto cached = App::cachedTracks(
            playlistTracks_, trackStates_, coverArtCache_, context_.sourceId, context_.playlist);
        if (cached) {
            for (const Playback::QueueEntry& entry : *cached)
                rows_.append(Row { entry.sourceId, entry.track, QDateTime() });
        }
    }
    emit rowsChanged();
    if (!context_.isRadio())
        fetch(context_).detach();
}

void OpenPlaylist::fillHistory()
{
    rows_.clear();
    for (const History::HistoryEntry& e : history_.entries())
        rows_.append(Row { e.sourceId, e.track, e.playedAt });
    emit rowsChanged();
}

void OpenPlaylist::playRow(int row)
{
    if (!context_.isValid() || row < 0 || row >= rows_.size())
        return;
    QVector<Playback::QueueEntry> entries;
    entries.reserve(rows_.size());
    for (const Row& r : std::as_const(rows_))
        entries.append(Playback::QueueEntry { r.sourceId, r.track });
    activePlaylist_.activate(context_, entries, row);
}

void OpenPlaylist::playAll()
{
    if (!context_.isValid())
        return;
    if (context_.isRadio())
        activePlaylist_.activate(context_, { }, 0);
    else
        playRow(0);
}

Rpc::Task<void> OpenPlaylist::fetch(PlaylistContext context)
{
    loading_ = true;
    emit loadingChanged(true);
    QList<Row> rows;
    bool failed = false;
    try {
        const QVector<Playback::QueueEntry> entries = co_await App::fetchTracks(
            sourceManager_, trackStates_, coverArtCache_, context.sourceId, context.playlist, &playlistTracks_);
        rows.reserve(entries.size());
        for (const Playback::QueueEntry& entry : entries)
            rows.append(Row { entry.sourceId, entry.track, QDateTime() });
    } catch (const std::exception& e) {
        qCWarning(lcOpenPlaylist) << "loading tracks failed for" << context.sourceId << ":" << e.what();
        const auto* call = dynamic_cast<const Rpc::RpcCallException*>(&e);
        if (call != nullptr && call->isOffline())
            messages_.offline(QString::fromStdString(e.what()));
        else
            messages_.error(QString::fromStdString(e.what()));
        failed = true;
    }
    // The user may have opened something else while this was loading.
    if (!context_.sameAs(context))
        co_return;
    // What's shown from the saved copy stays if the fresh list didn't come.
    if (!failed || rows_.isEmpty())
        rows_ = rows;
    loading_ = false;
    emit loadingChanged(false);
    emit rowsChanged();
}

void OpenPlaylist::applyEdit(
    const QString& sourceId, const Track& track, const QString& playlistId, bool added, int trackCount)
{
    if (context_.isHistory || context_.sourceId != sourceId || context_.playlist.id != playlistId)
        return;
    context_.playlist.trackCount = trackCount;
    if (added) {
        rows_.append(Row { sourceId, track, QDateTime() });
    } else {
        for (int i = 0; i < rows_.size(); ++i) {
            if (rows_[i].sourceId == sourceId && rows_[i].track.id == track.id) {
                rows_.removeAt(i);
                break;
            }
        }
    }
    emit rowsChanged();
}

} // namespace ViewModel
