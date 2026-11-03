#include "MobileCore.h"

#include "ProxyRouting.h"

namespace App {

MobileCore::MobileCore(const Paths& paths, QObject* parent)
    : QObject(parent)
    , settingsStore_(paths.settingsFile, QSettings::IniFormat)
    , settings_(settingsStore_)
{
    // As the desktop's main() does: the streams go through a local relay.
    Net::bypassProxyForLoopback();
    core_.sourceManager().startAll();
}

} // namespace App
