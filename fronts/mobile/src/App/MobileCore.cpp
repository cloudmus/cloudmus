#include "MobileCore.h"

#include <QDir>
#include <QUrl>

#include "PlaybackController.h"
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
    , playlistScreens_(core_)
    , trackScreen_(core_, mainScreen_, playlistScreens_)
    , mediaTree_(core_.sources(), core_.sourceManager(), library_, core_.coverArtCache(), core_.playback())
    , sourceSettings_(core_.sourceManager(), core_.settings())
    , settingsScreen_(core_, sourceSettings_)
    , sourceSettingsScreen_(core_, sourceSettings_)
#ifdef Q_OS_ANDROID
    , imitatedMusic_([this]() { return core_.playback().isPlaying(); })
    , imitatedPulse_(imitatedMusic_.source())
    , pulseScreen_(imitatedPulse_, core_.logoSound())
#else
    , pulseScreen_(core_.audioPulse(), core_.logoSound())
#endif
    , updates_(core_.settings())
    , updateScreen_(updates_)
{
    core_.messages().setOfflineShownElsewhere(true); // mainScreen_.offline()
    core_.activePlaylist().setRefreshDelay(paths.refreshDelayMs);
    core_.sourceManager().setSpawnDelay(paths.spawnDelayMs);
    leaveUnstartedWhatAskedForSignIn();
    applyPlaybackExperiments();
    // As the desktop's main() does: the streams go through a local relay.
    Net::bypassProxyForLoopback();
    core_.sourceManager().setHostMedia(paths.hostMedia);
    core_.sourceManager().startAll();
    setUpUpdates(paths.updatesDir);
}

void MobileCore::setUpUpdates(const QString& updatesDir)
{
    // As the desktop's --update-from and --update-feed: to try an update
    // with a feed of one's own (on Android, PlaybackService sets them from
    // update-feed.txt in the app's external files).
    const QString from = qEnvironmentVariable("CLOUDMUS_UPDATE_FROM");
    const QUrl testFeed(qEnvironmentVariable("CLOUDMUS_UPDATE_FEED"));
    const QString version = from.isEmpty() ? QStringLiteral(CLOUDMUS_MOBILE_VERSION) : from;
    // Developer tools' update server, if any (PlatformSettings::updateServer).
    auto configure = [this, version, testFeed] {
        QUrl feed = testFeed;
        const QString server = settings_.updateServer();
        if (feed.isEmpty() && !server.isEmpty()) {
            feed = QUrl::fromUserInput(server.contains(QLatin1String("://")) ? server
                                                                             : QStringLiteral("http://") + server);
            if (!feed.path().endsWith(QLatin1String(".json")))
                feed.setPath(QStringLiteral("/releases.json"));
        }
        if (feed.isValid() && !feed.isEmpty())
            updates_.configure(version, feed);
        else
            updates_.configure(version);
        // A test of an update is with builds between tags: checked at start too.
        updateScreen_.setCheckDevBuilds(!feed.isEmpty());
    };
    configure();
    connect(&settings_, &ViewModel::PlatformSettings::updateServerChanged, this, configure);
    if (updatesDir.isEmpty()) {
        updateScreen_.setInstallUnavailable(QString());
        return;
    }
    // What's left there is an update installed since, or one given up on.
    QDir dir(updatesDir);
    dir.removeRecursively();
    dir.mkpath(QStringLiteral("."));
    updateScreen_.setDownloadPath(
        [updatesDir](const Update::Release& release) { return updatesDir + QLatin1Char('/') + release.assetName; });
}

void MobileCore::applyPlaybackExperiments()
{
    Playback::PlaybackController& playback = core_.playback();
    auto applyOutput = [this, &playback] {
        // The other one stays behind it, for a device where this one fails.
        const QByteArray output
            = settings_.audioOutput() == QLatin1String("audiotrack") ? "audiotrack,opensles" : "opensles,audiotrack";
        playback.setOutput(output, settings_.outputChunkFrames());
    };
    auto applyEdges = [this, &playback] { playback.setEdges(settings_.fadeMs(), settings_.fadeSettleMs()); };
#ifdef Q_OS_ANDROID
    // The desktop's outputs have no such options: there it plays as the
    // desktop front does.
    applyOutput();
    connect(&settings_, &ViewModel::PlatformSettings::audioOutputChanged, this, applyOutput);
    connect(&settings_, &ViewModel::PlatformSettings::outputChunkFramesChanged, this, applyOutput);
#else
    Q_UNUSED(applyOutput)
#endif
    applyEdges();
    connect(&settings_, &ViewModel::PlatformSettings::fadeMsChanged, this, applyEdges);
    connect(&settings_, &ViewModel::PlatformSettings::fadeSettleMsChanged, this, applyEdges);
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
