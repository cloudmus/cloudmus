#include "MainScreen.h"

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
}

bool MainScreen::offline() const { return core_.connectivity().offline(); }

QString MainScreen::spaceWarning() const { return core_.preload().spaceWarning(); }

void MainScreen::setFilter(const QString& text) { filter_.setNeedle(text); }

void MainScreen::playRow(int row)
{
    const int source = filter_.sourceRow(row);
    if (source >= 0)
        core_.activePlaylist().playRow(source);
}

} // namespace ViewModel
