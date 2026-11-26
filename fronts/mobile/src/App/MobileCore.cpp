#include "MobileCore.h"

#include <QDateTime>
#include <QDir>
#include <QUrl>

#include "PlaybackController.h"
#include "ProxyRouting.h"
#include "SidebarModel.h"
#include "SourceManager.h"
#include "Sources.h"

namespace App {

MobileCore::MobileCore(const Paths& paths, QObject* parent)
    : QObject(parent)
    , settingsStore_(paths.settingsFile, QSettings::IniFormat)
    , settings_(settingsStore_)
    , library_(core_.sources(), core_.activePlaylist(), core_.nowPlaying(), core_.authStates(), core_.sourcePage(),
          core_.sourceManager(), core_.coverArtCache())
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
    // Until when the code a source showed is good. Following the link to
    // confirm it opens a browser, and a phone short of memory unloads the
    // whole app meanwhile: coming back, the source starts at once to go on
    // with that code (the backend keeps it), so it signs in by itself
    // instead of waiting to be opened and asking for a new one.
    const QString pendingGroup = QStringLiteral("signInPendingUntil");
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList unstarted = settingsStore_.value(key).toStringList();
    settingsStore_.beginGroup(pendingGroup);
    for (const QString& sourceId : settingsStore_.childKeys()) {
        if (settingsStore_.value(sourceId).toDateTime() > now) {
            unstarted.removeAll(sourceId);
            resumingSignIn_.append(sourceId);
        } else
            settingsStore_.remove(sourceId);
    }
    settingsStore_.endGroup();
    core_.sourceManager().setDeferredIds(unstarted);
    Rpc::AuthStates& states = core_.authStates();
    for (const QString& sourceId : unstarted)
        states.setSignedOut(sourceId); // it's what the Library shows for it
    connect(&states, &Rpc::AuthStates::changed, this, [this, key, pendingGroup, &states](const QString& sourceId) {
        const Rpc::AuthStates::State state = states.state(sourceId);
        const QString pendingKey = pendingGroup + QLatin1Char('/') + sourceId;
        const int expiresInSec = state.prompt.value(QStringLiteral("expiresInSec")).toInt();
        if (expiresInSec > 0)
            settingsStore_.setValue(pendingKey, QDateTime::currentDateTimeUtc().addSecs(expiresInSec));
        else
            settingsStore_.remove(pendingKey);
        // Back where it was left: the Library on the source signing in, not
        // on the first one. Once it's up and has its code again.
        if (!state.prompt.isEmpty() && resumingSignIn_.removeAll(sourceId) > 0)
            library_.selectSource(sourceId);
        updateSignInNotice();
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

MobileCore::SignInNotice MobileCore::signInNotice() const { return signInNotice_; }

void MobileCore::updateSignInNotice()
{
    SignInNotice notice;
    const Rpc::AuthStates& states = core_.authStates();
    const ViewModel::SidebarModel& model = core_.sources().model();
    QStringList waiting;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        if (index.data(ViewModel::SidebarModel::KindRole).toInt() != int(ViewModel::SidebarModel::Kind::SourceHeader))
            continue;
        const QString sourceId = index.data(ViewModel::SidebarModel::SourceIdRole).toString();
        if (!states.state(sourceId).prompt.value(QStringLiteral("code")).toString().isEmpty())
            waiting.append(sourceId);
    }
    if (!waiting.isEmpty()) {
        const QString sourceId = waiting.first();
        notice.title = tr("Signing in to %1").arg(model.sourceName(sourceId));
        notice.text = tr("Waiting for the code %1 to be confirmed")
                          .arg(states.state(sourceId).prompt.value(QStringLiteral("code")).toString());
        notice.channelName = tr("Sign-in");
    }
    if (notice == signInNotice_)
        return;
    signInNotice_ = notice;
    emit signInNoticeChanged();
}

} // namespace App
