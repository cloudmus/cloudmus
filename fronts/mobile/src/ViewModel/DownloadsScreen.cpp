#include "DownloadsScreen.h"

#include "Core.h"

namespace ViewModel {

DownloadsScreen::DownloadsScreen(App::Core& core, QObject* parent)
    : QObject(parent)
    , core_(core)
{
    connect(&core.downloads(), &Downloads::directoryChanged, this, &DownloadsScreen::directoryChanged);
}

bool DownloadsScreen::enabled() const { return core_.downloads().isEnabled(); }

QString DownloadsScreen::directory() const { return core_.downloads().directory(); }

int DownloadsScreen::layout() const { return int(core_.settings().downloadLayout()); }

void DownloadsScreen::setEnabled(bool on)
{
    core_.downloads().setEnabled(on);
    emit enabledChanged();
    emit directoryChanged();
}

void DownloadsScreen::setLayout(int layout)
{
    using Layout = Config::Settings::DownloadLayout;
    if (layout < int(Layout::Flat) || layout > int(Layout::ByArtistAlbum) || layout == this->layout())
        return;
    core_.settings().setDownloadLayout(Layout(layout));
    emit layoutChanged();
}

} // namespace ViewModel
