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
    , mediaTree_(core_.sources(), core_.sourceManager(), library_, core_.coverArtCache())
{
    core_.messages().setOfflineShownElsewhere(true); // mainScreen_.offline()
    core_.activePlaylist().setRefreshDelay(paths.refreshDelayMs);
    core_.sourceManager().setSpawnDelay(paths.spawnDelayMs);
    leaveUnstartedWhatAskedForSignIn();
    // As the desktop's main() does: the streams go through a local relay.
    Net::bypassProxyForLoopback();
    core_.sourceManager().startAll();
}

void MobileCore::leaveUnstartedWhatAskedForSignIn()
{
    // Each costs a Python and its memory just to say it wants a sign-in. It's
    // only known by having asked last time, so a source runs once to tell, then
    // waits until it's opened in the Library (ViewModel::LibraryScreen). Only
    // for asking: an error is no sign of a source that's not in use.
    const QString key = QStringLiteral("unstartedSources");
    const QStringList unstarted = settingsStore_.value(key).toStringList();
    core_.sourceManager().setDeferredIds(unstarted);
    Rpc::AuthStates& states = core_.authStates();
    for (const QString& sourceId : unstarted)
        states.setSignedOut(sourceId); // it's what the Library shows for it
    connect(&states, &Rpc::AuthStates::changed, this, [this, key, &states](const QString& sourceId) {
        const Rpc::AuthStates::State state = states.state(sourceId);
        QStringList list = settingsStore_.value(key).toStringList();
        const bool wanted = state.hasProblem && state.errorMessage.isEmpty();
        if (wanted == list.contains(sourceId))
            return;
        if (wanted)
            list.append(sourceId);
        else
            list.removeAll(sourceId);
        settingsStore_.setValue(key, list);
    });
}

} // namespace App
