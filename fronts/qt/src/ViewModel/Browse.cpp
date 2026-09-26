#include "Browse.h"

#include <QLoggingCategory>

#include "ActivePlaylist.h"
#include "Messages.h"
#include "PlaybackHistory.h"
#include "PlaylistEditing.h"
#include "Settings.h"
#include "Sources.h"
#include "TrackFetch.h"

namespace ViewModel {

namespace {
Q_LOGGING_CATEGORY(lcBrowse, "cloudmus.viewmodel.browse")
}

Browse::Browse(Rpc::SourceManager& sourceManager, Library::TrackStates& trackStates,
    Covers::CoverArtCache& coverArtCache, History::PlaybackHistory& history, Config::Settings& settings,
    Sources& sources, ActivePlaylist& activePlaylist, App::PlaylistEditing& playlistEditing, Messages& messages,
    QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , trackStates_(trackStates)
    , coverArtCache_(coverArtCache)
    , history_(history)
    , settings_(settings)
    , activePlaylist_(activePlaylist)
    , messages_(messages)
{
    // Playing something from here (or the sidebar) brings the main area
    // back to front.
    connect(&activePlaylist_, &ActivePlaylist::activated, this, &Browse::close);
    connect(&activePlaylist_, &ActivePlaylist::contextChanged, this, &Browse::remember);
    connect(&history_, &History::PlaybackHistory::changed, this, [this]() {
        // Refresh in place — a track just started playing.
        if (page_ == Page::Playlist && context_.isHistory)
            fillHistory();
    });
    connect(&playlistEditing, &App::PlaylistEditing::playlistEdited, this, &Browse::applyPlaylistEdit);
    connect(&sources, &Sources::sourceRemoved, this, [this](const QString& sourceId) {
        if ((page_ == Page::Source && sourceId_ == sourceId)
            || (page_ == Page::Playlist && !context_.isHistory && context_.sourceId == sourceId))
            close();
    });

    // The last run's page comes back once its source has listed its
    // playlists — History at once. (The active playlist restores itself
    // first, see ActivePlaylist, which is created before this.)
    pending_ = settings_.sidebarSelection();
    connect(&sources, &Sources::playlistsLoaded, this, &Browse::restoreFrom);
    if (pending_ == QStringLiteral("history")) {
        pending_.clear();
        if (!activePlaylist_.context().isHistory)
            openHistory();
    }
}

void Browse::openPlaylist(const QString& sourceId, const Playlist& playlist)
{
    pending_.clear();
    const PlaylistContext context { sourceId, playlist };
    // The active playlist is what the main area already shows — opening
    // it just gets the sheet out of the way.
    if (activePlaylist_.context().sameAs(context)) {
        close();
        return;
    }
    rows_.clear();
    setPage(Page::Playlist, context, QString());
    emit rowsChanged();
    if (!context.isRadio())
        load(context).detach();
}

void Browse::openHistory()
{
    pending_.clear();
    if (activePlaylist_.context().isHistory) {
        close();
        return;
    }
    setPage(Page::Playlist, activePlaylist_.historyContext(), QString());
    fillHistory();
}

void Browse::openSource(const QString& sourceId)
{
    pending_.clear();
    rows_.clear();
    setPage(Page::Source, PlaylistContext(), sourceId);
    emit rowsChanged();
}

void Browse::close()
{
    pending_.clear();
    if (page_ == Page::None)
        return;
    rows_.clear();
    if (loading_) {
        loading_ = false;
        emit loadingChanged(false);
    }
    setPage(Page::None, PlaylistContext(), QString());
    emit rowsChanged();
}

void Browse::playRow(int row)
{
    if (page_ != Page::Playlist || row < 0 || row >= rows_.size())
        return;
    QVector<Playback::QueueEntry> entries;
    entries.reserve(rows_.size());
    for (const Row& r : std::as_const(rows_))
        entries.append(Playback::QueueEntry { r.sourceId, r.track });
    activePlaylist_.activate(context_, entries, row);
}

void Browse::playAll()
{
    if (page_ != Page::Playlist)
        return;
    if (context_.isRadio())
        activePlaylist_.activate(context_, { }, 0);
    else
        playRow(0);
}

QString Browse::selectionKey() const
{
    if (page_ == Page::Source)
        return QStringLiteral("source:") + sourceId_;
    const PlaylistContext& context = page_ == Page::Playlist ? context_ : activePlaylist_.context();
    if (!context.isValid())
        return QString();
    if (context.isHistory)
        return QStringLiteral("history");
    return QStringLiteral("playlist:%1:%2").arg(context.sourceId, context.playlist.id);
}

void Browse::setPage(Page page, const PlaylistContext& context, const QString& sourceId)
{
    page_ = page;
    context_ = context;
    sourceId_ = sourceId;
    emit pageChanged();
    remember();
}

Rpc::Task<void> Browse::load(PlaylistContext context)
{
    loading_ = true;
    emit loadingChanged(true);
    QList<Row> rows;
    try {
        const QVector<Playback::QueueEntry> entries = co_await App::fetchTracks(
            sourceManager_, trackStates_, coverArtCache_, context.sourceId, context.playlist);
        rows.reserve(entries.size());
        for (const Playback::QueueEntry& entry : entries)
            rows.append(Row { entry.sourceId, entry.track, QDateTime() });
    } catch (const std::exception& e) {
        qCWarning(lcBrowse) << "loading tracks failed for" << context.sourceId << ":" << e.what();
        messages_.error(QString::fromStdString(e.what()));
    }
    // The user may have opened something else while this was loading.
    if (page_ != Page::Playlist || !context_.sameAs(context))
        co_return;
    rows_ = rows;
    loading_ = false;
    emit loadingChanged(false);
    emit rowsChanged();
}

void Browse::fillHistory()
{
    rows_.clear();
    for (const History::HistoryEntry& e : history_.entries())
        rows_.append(Row { e.sourceId, e.track, e.playedAt });
    emit rowsChanged();
}

void Browse::remember()
{
    // Not while the last run's is still waiting for its source to load:
    // what's selected until then (nothing, or the restored active
    // playlist) isn't the user's choice.
    if (pending_.isEmpty())
        settings_.setSidebarSelection(selectionKey());
}

void Browse::restoreFrom(const QString& sourceId, const QList<Playlist>& playlists)
{
    if (pending_.isEmpty())
        return;
    const QString key = pending_;
    if (key == QStringLiteral("source:") + sourceId) {
        openSource(sourceId);
        return;
    }
    const QString prefix = QStringLiteral("playlist:%1:").arg(sourceId);
    if (!key.startsWith(prefix))
        return;
    pending_.clear();
    const QString playlistId = key.mid(prefix.size());
    for (const Playlist& playlist : playlists) {
        if (playlist.id != playlistId)
            continue;
        // The active playlist is already in the main area — selecting it
        // is all there is to do.
        if (!activePlaylist_.context().sameAs(PlaylistContext { sourceId, playlist }))
            openPlaylist(sourceId, playlist);
        break;
    }
}

void Browse::applyPlaylistEdit(
    const QString& sourceId, const Track& track, const QString& playlistId, bool added, int trackCount)
{
    if (page_ != Page::Playlist || context_.isHistory || context_.sourceId != sourceId
        || context_.playlist.id != playlistId)
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
