#include "PlaylistScreen.h"

#include <QUrl>

#include <algorithm>

#include "Core.h"

namespace ViewModel {

PlaylistScreen::PlaylistScreen(App::Core& core, QObject* parent)
    : QObject(parent)
    , core_(core)
    , open_(core.sourceManager(), core.trackStates(), core.coverArtCache(), core.playbackHistory(),
          core.activePlaylist(), core.playlistEditing(), core.messages())
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
    filter_.setNeedle(QString());
    open_.openHistory();
    announce();
}

void PlaylistScreen::close()
{
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
}

} // namespace ViewModel
