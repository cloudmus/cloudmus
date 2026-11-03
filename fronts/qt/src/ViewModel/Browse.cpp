#include "Browse.h"

#include "ActivePlaylist.h"
#include "Settings.h"
#include "Sources.h"

namespace ViewModel {

Browse::Browse(Rpc::SourceManager& sourceManager, Library::TrackStates& trackStates,
    Covers::CoverArtCache& coverArtCache, History::PlaybackHistory& history, Config::Settings& settings,
    Sources& sources, ActivePlaylist& activePlaylist, App::PlaylistEditing& playlistEditing, Messages& messages,
    QObject* parent)
    : QObject(parent)
    , settings_(settings)
    , activePlaylist_(activePlaylist)
    , open_(sourceManager, trackStates, coverArtCache, history, activePlaylist, playlistEditing, messages)
{
    connect(&open_, &OpenPlaylist::rowsChanged, this, &Browse::rowsChanged);
    connect(&open_, &OpenPlaylist::loadingChanged, this, &Browse::loadingChanged);
    // Playing something from here (or the sidebar) brings the main area
    // back to front.
    connect(&activePlaylist_, &ActivePlaylist::activated, this, &Browse::close);
    connect(&activePlaylist_, &ActivePlaylist::contextChanged, this, &Browse::remember);
    connect(&sources, &Sources::sourceRemoved, this, [this](const QString& sourceId) {
        if ((page_ == Page::Source && sourceId_ == sourceId)
            || (page_ == Page::Playlist && !open_.context().isHistory && open_.context().sourceId == sourceId))
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
    open_.setContext(context);
    setPage(Page::Playlist, QString());
    open_.load();
}

void Browse::openHistory()
{
    pending_.clear();
    if (activePlaylist_.context().isHistory) {
        close();
        return;
    }
    open_.setContext(activePlaylist_.historyContext());
    setPage(Page::Playlist, QString());
    open_.fillHistory();
}

void Browse::openSource(const QString& sourceId)
{
    pending_.clear();
    open_.setContext(PlaylistContext());
    setPage(Page::Source, sourceId);
    open_.announceRows();
}

void Browse::close()
{
    pending_.clear();
    if (page_ == Page::None)
        return;
    open_.setContext(PlaylistContext());
    setPage(Page::None, QString());
    open_.announceRows();
}

void Browse::playRow(int row)
{
    if (page_ == Page::Playlist)
        open_.playRow(row);
}

void Browse::playAll()
{
    if (page_ == Page::Playlist)
        open_.playAll();
}

QString Browse::selectionKey() const
{
    if (page_ == Page::Source)
        return QStringLiteral("source:") + sourceId_;
    const PlaylistContext& context = page_ == Page::Playlist ? open_.context() : activePlaylist_.context();
    if (!context.isValid())
        return QString();
    if (context.isHistory)
        return QStringLiteral("history");
    return QStringLiteral("playlist:%1:%2").arg(context.sourceId, context.playlist.id);
}

void Browse::setPage(Page page, const QString& sourceId)
{
    page_ = page;
    sourceId_ = sourceId;
    emit pageChanged();
    remember();
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

} // namespace ViewModel
