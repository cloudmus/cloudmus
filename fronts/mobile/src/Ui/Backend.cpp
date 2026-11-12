#include "Backend.h"

#include <QAbstractItemModelReplica>
#include <QLoggingCategory>
#include <QRemoteObjectDynamicReplica>
#include <QRemoteObjectNode>
#include <QUrl>

#include "Log.h"
#include "Names.h"

#ifndef Q_OS_ANDROID
#include "MobileCore.h"
#endif

namespace Ui {

namespace {
Q_LOGGING_CATEGORY(lcBackend, "cloudmus.ui.backend")

constexpr int kRestartIntervalMs = 3000;
}

#ifndef Q_OS_ANDROID
Backend::Backend(App::MobileCore& core, QObject* parent)
    : QObject(parent)
    , nowPlaying_(&core.nowPlaying())
    , playlist_(&core.activePlaylist())
    , settings_(&core.settings())
#ifdef CLOUDMUS_MOBILE_SHARED_CORE
    , library_(&core.library())
    , mainScreen_(&core.mainScreen())
    , downloadsScreen_(&core.downloadsScreen())
    , downloads_(&core.downloadsScreen().rows())
    , playlistScreen_(&core.playlistScreen())
    , trackScreen_(&core.trackScreen())
    , settingsScreen_(&core.settingsScreen())
    , sourceSettings_(&core.sourceSettingsScreen())
#endif
    , tracks_(&core.tracks())
#ifdef CLOUDMUS_MOBILE_SHARED_CORE
    , sources_(&core.library().sourceRows())
    , favorites_(&core.library().favoriteRows())
    , otherPlaylists_(&core.library().otherRows())
    , openTracks_(&core.playlistScreen().rows())
    , trackPlaylists_(&core.trackScreen().playlistRows())
    , settingsSources_(&core.settingsScreen().sourceRows())
    , settingsProxies_(&core.settingsScreen().proxyRows())
    , sourceConnections_(&core.sourceSettingsScreen().connectionRows())
    , sourceFields_(&core.sourceSettingsScreen().fieldRows())
#endif
    , ready_(true)
{
}
#endif

Backend::Backend(const QString& socketPath, QObject* parent)
    : QObject(parent)
    , node_(std::make_unique<QRemoteObjectNode>())
{
    const QUrl url(QStringLiteral("local:") + socketPath);
    if (!node_->connectToNode(url))
        qCWarning(lcBackend) << "can't connect to the core at" << socketPath << node_->lastError();
    qCInfo(lcBackend) << "connecting to the core at" << socketPath;

    acquire(nowPlaying_, Remote::Names::nowPlaying);
    acquire(playlist_, Remote::Names::activePlaylist);
    acquire(settings_, Remote::Names::settings);

    acquireModel(tracks_, Remote::Names::tracks);
#ifdef CLOUDMUS_MOBILE_SHARED_CORE
    acquire(library_, Remote::Names::library);
    acquireModel(sources_, Remote::Names::sources);
    acquireModel(favorites_, Remote::Names::favorites);
    acquireModel(otherPlaylists_, Remote::Names::otherPlaylists);
    acquire(mainScreen_, Remote::Names::mainScreen);
    acquire(downloadsScreen_, Remote::Names::downloadsScreen);
    acquireModel(downloads_, Remote::Names::downloads);
    acquire(playlistScreen_, Remote::Names::playlistScreen);
    acquireModel(openTracks_, Remote::Names::openTracks);
    acquire(trackScreen_, Remote::Names::trackScreen);
    acquireModel(trackPlaylists_, Remote::Names::trackPlaylists);
    acquire(settingsScreen_, Remote::Names::settingsScreen);
    acquireModel(settingsSources_, Remote::Names::settingsSources);
    acquireModel(settingsProxies_, Remote::Names::settingsProxies);
    acquire(sourceSettings_, Remote::Names::sourceSettings);
    acquireModel(sourceConnections_, Remote::Names::sourceConnections);
    acquireModel(sourceFields_, Remote::Names::sourceFields);
#endif

    // The core may be slow to come up, or gone (its process killed while
    // the screen was off): ask for it again until it answers.
    restartTimer_.setInterval(kRestartIntervalMs);
    connect(&restartTimer_, &QTimer::timeout, this, [this] {
        qCInfo(lcBackend) << "the core doesn't answer; starting it again";
        if (restartCore_)
            restartCore_();
    });
    restartTimer_.start();
}

void Backend::setRestartCore(std::function<void()> restart) { restartCore_ = std::move(restart); }

Backend::~Backend()
{
    // Replicas before their node: they unregister from it as they go.
    if (node_) {
        delete nowPlaying_.data();
        delete playlist_.data();
        delete settings_.data();
        delete library_.data();
        delete tracks_.data();
        delete sources_.data();
        delete favorites_.data();
        delete otherPlaylists_.data();
        delete mainScreen_.data();
        delete downloadsScreen_.data();
        delete downloads_.data();
        delete playlistScreen_.data();
        delete openTracks_.data();
        delete trackScreen_.data();
        delete trackPlaylists_.data();
        delete settingsScreen_.data();
        delete settingsSources_.data();
        delete settingsProxies_.data();
        delete sourceSettings_.data();
        delete sourceConnections_.data();
        delete sourceFields_.data();
    }
}

void Backend::acquire(QPointer<QObject>& slot, const QString& name)
{
    QRemoteObjectDynamicReplica* replica = node_->acquireDynamic(name);
    replica->setParent(this);
    slot = replica;
    replicas_.append(replica);
    // A dynamic replica only has its properties once the source has sent
    // them (Valid); QML binds to the meta-object it sees, so it must wait.
    connect(replica, &QRemoteObjectReplica::stateChanged, this, [this, name](QRemoteObjectReplica::State state) {
        qCInfo(lcBackend) << name << "replica:" << state;
        updateReady();
    });
}

void Backend::acquireModel(QPointer<QAbstractItemModel>& slot, const QString& name)
{
    auto* model = node_->acquireModel(name);
    model->setParent(this);
    slot = model;
    ++modelsAcquired_;
    connect(model, &QAbstractItemModelReplica::initialized, this, [this] {
        ++modelsInitialized_;
        updateReady();
    });
}

void Backend::updateReady()
{
    bool ready = modelsInitialized_ >= modelsAcquired_;
    for (const QRemoteObjectReplica* replica : std::as_const(replicas_))
        ready = ready && replica->state() == QRemoteObjectReplica::Valid;
    if (ready == ready_)
        return;
    ready_ = ready;
    if (ready) {
        restartTimer_.stop();
        if (!everReady_)
            Logging::mark(QStringLiteral("core replicas ready"));
        else
            qCInfo(lcBackend) << "the core is back";
        everReady_ = true;
    } else {
        qCWarning(lcBackend) << "lost the core";
        restartTimer_.start();
    }
    emit readyChanged();
}

} // namespace Ui
