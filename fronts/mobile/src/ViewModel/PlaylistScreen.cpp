#include "PlaylistScreen.h"

#include <QUrl>

#include <algorithm>

#include "Core.h"

namespace ViewModel {

PlaylistScreen::PlaylistScreen(App::Core& core, QObject* parent)
    : QObject(parent)
    , core_(core)
    , open_(core.sourceManager(), core.trackStates(), core.coverArtCache(), core.playbackHistory(),
          core.activePlaylist(), core.playlistEditing(), core.messages(), core.playlistTracks())
    , folders_({ "playlistId", "title", "trackCount", "coverUrl" })
{
    tracks_.setTrackStates(&core.trackStates());
    tracks_.setPreload(&core.preload());
    tracks_.setCovers(&core.coverArtCache());
    filter_.setSourceModel(&tracks_);

    connect(&open_, &OpenPlaylist::rowsChanged, this, &PlaylistScreen::refill);
    connect(&open_, &OpenPlaylist::loadingChanged, this, &PlaylistScreen::loadingChanged);
    connect(&core.nowPlaying(), &NowPlaying::trackChanged, this, &PlaylistScreen::markCurrent);
    connect(&core.sources(), &Sources::sourceChanged, this, &PlaylistScreen::announce);
    connect(&core.coverArtCache(), &Covers::CoverArtCache::pixmapReady, this, &PlaylistScreen::coverUrlChanged);
    connect(&core.trackDeletion(), &App::TrackDeletion::deleted, &open_, &OpenPlaylist::dropTracks);
    connect(&core.sources(), &Sources::sourceRemoved, this, [this](const QString& sourceId) {
        if (!open_.context().isHistory && open_.context().sourceId == sourceId) {
            close();
            emit closed();
        }
    });
}

QString PlaylistScreen::title() const
{
    const PlaylistContext& context = open_.context();
    return context.isHistory ? tr("History") : context.playlist.title;
}

QString PlaylistScreen::coverUrl() const
{
    const QString file = core_.coverArtCache().localFile(open_.context().playlist.coverUrl.value_or(QString()));
    return file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString();
}

bool PlaylistScreen::isFavorite() const
{
    const PlaylistContext& context = open_.context();
    return !context.isHistory && core_.sources().isFavorite(context.sourceId, context.playlist.id);
}

bool PlaylistScreen::canDownload() const
{
    const PlaylistContext& context = open_.context();
    return !context.isHistory && core_.trackActions().canDownloadPlaylist(context.sourceId, context.playlist);
}

void PlaylistScreen::openPlaylist(const QString& sourceId, const QString& playlistId)
{
    trail_.clear();
    show(sourceId, playlistId);
}

void PlaylistScreen::openFolder(const QString& playlistId)
{
    const PlaylistContext& context = open_.context();
    if (context.isHistory || context.sourceId.isEmpty())
        return;
    trail_.append(context.playlist.id);
    show(context.sourceId, playlistId);
}

bool PlaylistScreen::canDeletePlaylist() const
{
    const PlaylistContext& context = open_.context();
    return !context.isHistory && core_.trackDeletion().canDelete(context.sourceId, context.playlist);
}

bool PlaylistScreen::canDeleteTracks() const
{
    return std::any_of(open_.rows().cbegin(), open_.rows().cend(), [this](const OpenPlaylist::Row& row) {
        return core_.trackDeletion().canDelete(row.sourceId, row.track);
    });
}

void PlaylistScreen::deleteRows(const QVariantList& rows)
{
    // By source: History mixes them.
    QHash<QString, QStringList> bySource;
    for (const QVariant& value : rows) {
        const int row = filter_.sourceRow(value.toInt());
        if (row < 0 || row >= open_.rows().size())
            continue;
        const OpenPlaylist::Row& entry = open_.rows().at(row);
        if (core_.trackDeletion().canDelete(entry.sourceId, entry.track))
            bySource[entry.sourceId].append(entry.track.id);
    }
    for (auto it = bySource.cbegin(); it != bySource.cend(); ++it)
        core_.trackDeletion().deleteTracks(it.key(), it.value());
}

void PlaylistScreen::deletePlaylist()
{
    if (!canDeletePlaylist())
        return;
    const PlaylistContext& context = open_.context();
    core_.trackDeletion().deletePlaylist(context.sourceId, context.playlist);
    // It's gone: back where it was opened from.
    if (!trail_.isEmpty())
        goBack();
}

void PlaylistScreen::goBack()
{
    if (trail_.isEmpty())
        return;
    show(open_.context().sourceId, trail_.takeLast());
}

void PlaylistScreen::show(const QString& sourceId, const QString& playlistId)
{
    const QList<Playlist> playlists = core_.sources().playlists(sourceId);
    const auto it = std::find_if(
        playlists.cbegin(), playlists.cend(), [&](const Playlist& playlist) { return playlist.id == playlistId; });
    if (it == playlists.cend())
        return;
    filter_.setNeedle(QString());
    open_.open(sourceId, *it);
    announce();
}

void PlaylistScreen::openHistory()
{
    trail_.clear();
    filter_.setNeedle(QString());
    open_.openHistory();
    announce();
}

void PlaylistScreen::close()
{
    trail_.clear();
    open_.close();
    announce();
}

void PlaylistScreen::playRow(int row) { open_.playRow(filter_.sourceRow(row)); }

void PlaylistScreen::playAll() { open_.playAll(); }

void PlaylistScreen::setFilter(const QString& text) { filter_.setNeedle(text); }

void PlaylistScreen::toggleFavorite()
{
    const PlaylistContext& context = open_.context();
    if (!context.isHistory)
        core_.sources().toggleFavorite(context.sourceId, context.playlist.id);
}

void PlaylistScreen::downloadAll()
{
    if (canDownload())
        core_.downloads().downloadPlaylist(open_.context().sourceId, open_.context().playlist);
}

void PlaylistScreen::refill()
{
    QList<TrackListModel::MixedSourceEntry> rows;
    rows.reserve(open_.rows().size());
    for (const OpenPlaylist::Row& row : open_.rows())
        rows.append({ row.sourceId, row.track, row.playedAt });
    tracks_.setMixedSourceTracks(rows);
    markCurrent();
    emit countChanged();
    emit canDeleteTracksChanged();
}

void PlaylistScreen::markCurrent()
{
    const NowPlaying& now = core_.nowPlaying();
    tracks_.setCurrentTrack(now.sourceId(), now.hasTrack() ? now.track().id : QString());
}

void PlaylistScreen::announce()
{
    emit titleChanged();
    emit countChanged();
    emit coverUrlChanged();
    emit isRadioChanged();
    emit isHistoryChanged();
    emit isFavoriteChanged();
    emit canDownloadChanged();
    emit canGoBackChanged();
    emit canDeletePlaylistChanged();
    emit canDeleteTracksChanged();
    rebuildFolders();
}

void PlaylistScreen::rebuildFolders()
{
    QList<QVariantHash> rows;
    const PlaylistContext& context = open_.context();
    if (!context.isHistory && !context.sourceId.isEmpty()) {
        for (const Playlist& playlist : core_.sources().playlists(context.sourceId)) {
            if (playlist.parentId != context.playlist.id)
                continue;
            const QString cover = core_.coverArtCache().localFile(playlist.coverUrl.value_or(QString()));
            rows.append({
                { QStringLiteral("playlistId"), playlist.id },
                { QStringLiteral("title"), playlist.title },
                { QStringLiteral("trackCount"), playlist.trackCount },
                { QStringLiteral("coverUrl"), cover.isEmpty() ? QString() : QUrl::fromLocalFile(cover).toString() },
            });
        }
    }
    folders_.setRows(rows);
}

} // namespace ViewModel
