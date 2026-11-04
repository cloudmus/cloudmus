#include "MainScreen.h"

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

void MainScreen::setFilter(const QString& text) { filter_.setNeedle(text); }

void MainScreen::playRow(int row)
{
    const int source = filter_.sourceRow(row);
    if (source >= 0)
        core_.activePlaylist().playRow(source);
}

} // namespace ViewModel
