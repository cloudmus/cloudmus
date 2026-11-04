#include "MobileCore.h"

#include "ProxyRouting.h"

namespace App {

MobileCore::MobileCore(const Paths& paths, QObject* parent)
    : QObject(parent)
    , settingsStore_(paths.settingsFile, QSettings::IniFormat)
    , settings_(settingsStore_)
    , library_(core_.sources(), core_.activePlaylist(), core_.nowPlaying(), core_.authStates(), core_.sourcePage(),
          core_.coverArtCache())
    , mainScreen_(core_)
    , downloadsScreen_(core_)
    , playlistScreen_(core_)
    , trackScreen_(core_, mainScreen_, playlistScreen_)
{
    core_.messages().setOfflineShownElsewhere(true); // mainScreen_.offline()
    // As the desktop's main() does: the streams go through a local relay.
    Net::bypassProxyForLoopback();
    core_.sourceManager().startAll();
}

} // namespace App
