#include "TrackScreen.h"

#include <QUrl>

#include <algorithm>

#include "Core.h"
#include "MainScreen.h"
#include "PlaylistScreen.h"

namespace ViewModel {

TrackScreen::TrackScreen(App::Core& core, MainScreen& mainScreen, PlaylistScreen& playlist, QObject* parent)
    : QObject(parent)
    , core_(core)
    , mainScreen_(mainScreen)
    , playlist_(playlist)
    , playlistRows_({ "playlistId", "title", "contains", "changing" })
{
    connect(&core.trackStates(), &Library::TrackStates::changed, this,
        [this](const QString& sourceId, const QString& trackId) {
            if (sourceId == sourceId_ && trackId == track_.id)
                announceState();
        });
    connect(&core.trackStates(), &Library::TrackStates::bulkChanged, this, &TrackScreen::announceState);
    connect(&core.coverArtCache(), &Covers::CoverArtCache::pixmapReady, this, &TrackScreen::coverUrlChanged);
}

QString TrackScreen::artist() const
{
    QStringList names;
    for (const Artist& artist : track_.artists)
        names.append(artist.name);
    return names.join(QStringLiteral(", "));
}

QString TrackScreen::coverUrl() const
{
    const QString cover
        = track_.coverUrl.value_or(track_.album ? track_.album->coverUrl.value_or(QString()) : QString());
    const QString file = core_.coverArtCache().localFile(cover);
    return file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString();
}

QString TrackScreen::seed() const { return NowPlaying::backgroundSeed(track_); }

QString TrackScreen::sourceName() const { return core_.sources().model().sourceName(sourceId_); }

QString TrackScreen::quality() const
{
    const auto quality = core_.trackStates().state(sourceId_, track_.id).streamQuality;
    if (!quality)
        return { };
    QStringList parts;
    if (!quality->codec.isEmpty())
        parts.append(quality->codec);
    if (quality->bitrateKbps > 0)
        parts.append(tr("%1 kbps").arg(quality->bitrateKbps));
    return parts.join(QStringLiteral(" · "));
}

QDateTime TrackScreen::lastPlayedAt() const { return core_.trackStates().state(sourceId_, track_.id).lastPlayedAt; }

bool TrackScreen::liked() const { return core_.trackStates().state(sourceId_, track_.id).liked.value_or(false); }

bool TrackScreen::disliked() const { return core_.trackStates().state(sourceId_, track_.id).disliked.value_or(false); }

bool TrackScreen::canLike() const { return core_.trackActions().available(sourceId_).like; }

bool TrackScreen::canDislike() const { return core_.trackActions().available(sourceId_).dislike; }

bool TrackScreen::canStartRadio() const { return core_.trackActions().available(sourceId_).radio; }

bool TrackScreen::canDownload() const { return core_.trackActions().available(sourceId_).download; }

bool TrackScreen::canEditPlaylists() const { return core_.trackActions().available(sourceId_).playlists; }

void TrackScreen::showActiveRow(int filteredRow)
{
    const int row = mainScreen_.rows().sourceRow(filteredRow);
    TrackListModel& tracks = core_.activeTracks();
    if (row < 0 || row >= tracks.rowCount())
        return;
    show(tracks.sourceIdAt(row), tracks.trackAt(row), [this, row] { core_.activePlaylist().playRow(row); });
}

void TrackScreen::showOpenRow(int filteredRow)
{
    const int row = playlist_.rows().sourceRow(filteredRow);
    const TrackListModel& tracks = playlist_.allRows();
    if (row < 0 || row >= tracks.rowCount())
        return;
    show(tracks.sourceIdAt(row), tracks.trackAt(row), [this, row] { playlist_.open().playRow(row); });
}

void TrackScreen::showPlaying()
{
    const NowPlaying& now = core_.nowPlaying();
    if (now.hasTrack())
        show(now.sourceId(), now.track(), { });
}

void TrackScreen::show(const QString& sourceId, const Track& track, std::function<void()> play)
{
    sourceId_ = sourceId;
    track_ = track;
    play_ = std::move(play);
    playlists_.clear();
    containing_.clear();
    changing_.clear();
    showPlaylists();
    // A cached track's quality comes from its file, when asked for.
    core_.trackStates().requestStreamQuality(sourceId_, track_.id);
    announce();
}

void TrackScreen::play()
{
    if (play_)
        play_();
}

void TrackScreen::playNext() { core_.trackActions().playNext(sourceId_, track_); }

void TrackScreen::addToQueue() { core_.trackActions().addToQueue(sourceId_, track_); }

void TrackScreen::toggleLiked()
{
    core_.nowPlaying().setTrackLiked(sourceId_, track_.id, !liked(), /*announce=*/true).detach();
}

void TrackScreen::toggleDisliked()
{
    core_.nowPlaying().setTrackDisliked(sourceId_, track_.id, !disliked(), /*announce=*/true).detach();
}

void TrackScreen::startRadio(const QString& title) { core_.trackActions().startRadio(sourceId_, track_, title); }

void TrackScreen::download() { core_.downloads().downloadTrack(sourceId_, track_); }

void TrackScreen::loadPlaylists() { fetchPlaylists(sourceId_, track_).detach(); }

Rpc::Task<void> TrackScreen::fetchPlaylists(QString sourceId, Track track)
{
    playlistsLoading_ = true;
    playlistsFailed_ = false;
    emit playlistsLoadingChanged();
    emit playlistsFailedChanged();
    const auto membership = co_await core_.playlistEditing().membership(sourceId, track.id);
    // The sheet may have moved on to another track meanwhile.
    if (sourceId != sourceId_ || track.id != track_.id)
        co_return;
    playlistsLoading_ = false;
    playlistsFailed_ = !membership.has_value();
    if (membership) {
        playlists_ = membership->playlists;
        containing_ = membership->containing;
    }
    showPlaylists();
    emit playlistsLoadingChanged();
    emit playlistsFailedChanged();
}

void TrackScreen::setInPlaylist(const QString& playlistId, bool in)
{
    const auto it = std::find_if(
        playlists_.cbegin(), playlists_.cend(), [&](const Playlist& playlist) { return playlist.id == playlistId; });
    if (it != playlists_.cend() && !changing_.contains(playlistId))
        changePlaylist(sourceId_, track_, *it, in).detach();
}

Rpc::Task<void> TrackScreen::changePlaylist(QString sourceId, Track track, Playlist playlist, bool in)
{
    changing_.append(playlist.id);
    showPlaylists();
    const bool done = co_await core_.playlistEditing().setTrackInPlaylist(sourceId, track, playlist, in);
    changing_.removeAll(playlist.id);
    if (sourceId == sourceId_ && track.id == track_.id && done) {
        if (in)
            containing_.append(playlist.id);
        else
            containing_.removeAll(playlist.id);
    }
    showPlaylists();
}

void TrackScreen::showPlaylists()
{
    QList<QVariantHash> rows;
    for (const Playlist& playlist : std::as_const(playlists_)) {
        rows.append({
            { QStringLiteral("playlistId"), playlist.id },
            { QStringLiteral("title"), playlist.title },
            { QStringLiteral("contains"), containing_.contains(playlist.id) },
            { QStringLiteral("changing"), changing_.contains(playlist.id) },
        });
    }
    playlistRows_.setRows(rows);
}

void TrackScreen::announceState()
{
    emit likedChanged();
    emit dislikedChanged();
    emit qualityChanged();
    emit lastPlayedAtChanged();
}

void TrackScreen::announce()
{
    emit titleChanged();
    emit artistChanged();
    emit albumChanged();
    emit durationMsChanged();
    emit coverUrlChanged();
    emit seedChanged();
    emit sourceNameChanged();
    emit isExplicitChanged();
    emit webUrlChanged();
    emit canPlayChanged();
    emit canLikeChanged();
    emit canDislikeChanged();
    emit canStartRadioChanged();
    emit canDownloadChanged();
    emit canEditPlaylistsChanged();
    announceState();
}

} // namespace ViewModel
