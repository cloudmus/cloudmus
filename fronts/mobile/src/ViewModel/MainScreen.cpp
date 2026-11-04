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
        [this](Messages::Kind kind, const QString& text) { emit message(int(kind), text); });
    connect(&core.connectivity(), &App::Connectivity::offlineChanged, this, &MainScreen::offlineChanged);
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
}

bool MainScreen::offline() const { return core_.connectivity().offline(); }

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
