#include "MainScreen.h"

#include <QUrl>

#include <utility>

#include "PlaybackController.h"
#include "Preload.h"

#include "Core.h"

namespace ViewModel {

MainScreen::MainScreen(App::Core& core, QObject* parent)
    : QObject(parent)
    , core_(core)
{
    filter_.setSourceModel(&core.activeTracks());
    connect(&core.messages(), &Messages::posted, this,
        [this](Messages::Kind kind, const QString& text, const QString& details) {
            emit message(int(kind), text, details);
        });
    connect(&core.connectionStatus(), &ConnectionStatus::changed, this, &MainScreen::offlineChanged);
    connect(&core.connectionStatus(), &ConnectionStatus::checkingChanged, this, &MainScreen::checkingConnectionChanged);
    connect(&core.preload(), &Preload::spaceWarningChanged, this, &MainScreen::spaceWarningChanged);
    connect(&core.preload(), &Preload::progressChanged, this, &MainScreen::preloadFractionsChanged);
    connect(&core.playback(), &Playback::PlaybackController::trackChanged, this, [this]() {
        const int current = core_.playback().currentIndex();
        const QModelIndex source = core_.activeTracks().index(current);
        emit playingRowChanged(source.isValid() ? filter_.mapFromSource(source).row() : -1);
    });
    const auto refresh = [this]() { refreshNeighbors(); };
    connect(&core.playback(), &Playback::PlaybackController::trackChanged, this, refresh);
    connect(&core.playback(), &Playback::PlaybackController::queueChanged, this, refresh);
    connect(&core.nowPlaying(), &NowPlaying::playModesChanged, this, refresh);
    connect(&core.coverArtCache(), &Covers::CoverArtCache::pixmapReady, this, refresh);
    // Downloads turned on or off, a source coming up, another playlist playing.
    connect(&core.downloads(), &Downloads::changed, this, &MainScreen::readDownload);
    connect(&core.activePlaylist(), &ActivePlaylist::contextChanged, this, &MainScreen::readDownload);
    connect(&core.sources(), &Sources::sourceChanged, this, &MainScreen::readDownload);
    readDownload();
}

void MainScreen::downloadPlaylist()
{
    const PlaylistContext& context = core_.activePlaylist().context();
    if (canDownload_)
        core_.downloads().downloadPlaylist(context.sourceId, context.playlist);
}

void MainScreen::readDownload()
{
    const PlaylistContext& context = core_.activePlaylist().context();
    const bool can = !context.isHistory && !context.isRadio()
        && core_.trackActions().canDownloadPlaylist(context.sourceId, context.playlist);
    const Downloads::Job* job
        = context.isHistory ? nullptr : core_.downloads().playlistJob(context.sourceId, context.playlist.id);
    const bool downloading = job != nullptr;
    const double progress = job != nullptr ? job->progress() : -1;
    if (can != canDownload_) {
        canDownload_ = can;
        emit canDownloadPlaylistChanged();
    }
    if (downloading != downloading_) {
        downloading_ = downloading;
        emit playlistDownloadingChanged();
    }
    if (progress != downloadProgress_) {
        downloadProgress_ = progress;
        emit playlistDownloadProgressChanged();
    }
}

bool MainScreen::offline() const { return core_.connectionStatus().visible(); }

QString MainScreen::connectionText() const { return core_.connectionStatus().text(); }

bool MainScreen::noNetwork() const { return core_.connectionStatus().state() == ConnectionStatus::NoNetwork; }

bool MainScreen::checkingConnection() const { return core_.connectionStatus().checking(); }

void MainScreen::checkConnection()
{
    ConnectionStatus& status = core_.connectionStatus();
    status.checkNow();
    emit message(int(Messages::Kind::Info), status.text(), status.details());
}

void MainScreen::setViewShown(bool shown)
{
    core_.connectionStatus().setViewShown(shown);
    // With nothing loaded no track change brings the list to the saved track.
    if (shown && !core_.playback().hasCurrentTrack()) {
        const QModelIndex source = core_.activeTracks().index(core_.activePlaylist().savedRow());
        if (source.isValid())
            emit playingRowChanged(filter_.mapFromSource(source).row());
    }
}

QString MainScreen::spaceWarning() const { return core_.preload().spaceWarning(); }

QVariantList MainScreen::preloadFractions() const
{
    QVariantList fractions;
    for (const Preload::Slot& slot : core_.preload().tracks())
        fractions.append(slot.fraction);
    return fractions;
}

MainScreen::Neighbor MainScreen::neighbor(int delta) const
{
    Playback::PlaybackController& playback = core_.playback();
    const int index = playback.neighborIndex(delta);
    if (index < 0 || index >= playback.queue().size())
        return { };
    const Track& track = playback.queue().at(index).track;
    const QString cover = core_.coverArtCache().localFile(
        track.coverUrl.value_or(track.album ? track.album->coverUrl.value_or(QString()) : QString()));
    return { true, cover.isEmpty() ? QString() : QUrl::fromLocalFile(cover).toString(), NowPlaying::backgroundSeed(track) };
}

bool MainScreen::radioGoesOn() const { return core_.playback().isRadio() && core_.playback().hasCurrentTrack(); }

void MainScreen::refreshNeighbors()
{
    const auto update = [](Neighbor& current, const Neighbor& fresh, auto&& exists, auto&& cover, auto&& seed) {
        const Neighbor before = std::exchange(current, fresh);
        if (before.exists != fresh.exists)
            exists();
        if (before.coverUrl != fresh.coverUrl)
            cover();
        if (before.seed != fresh.seed)
            seed();
    };
    update(next_, neighbor(1), [this]() { emit canGoNextChanged(); }, [this]() { emit nextCoverUrlChanged(); },
        [this]() { emit nextSeedChanged(); });
    update(previous_, neighbor(-1), [this]() { emit canGoPreviousChanged(); }, [this]() { emit previousCoverUrlChanged(); },
        [this]() { emit previousSeedChanged(); });
    // A station's going on doesn't change with the rows.
    emit canGoNextChanged();
}

void MainScreen::setFilter(const QString& text) { filter_.setNeedle(text); }

void MainScreen::playRow(int row)
{
    const int source = filter_.sourceRow(row);
    if (source >= 0)
        core_.activePlaylist().playRow(source);
}

} // namespace ViewModel
