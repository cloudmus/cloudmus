#include "Backend.h"

#include <QAbstractItemModelReplica>
#include <QJSEngine>
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
    , library_(&core.library())
    , mainScreen_(&core.mainScreen())
    , downloadsScreen_(&core.downloadsScreen())
    , playlistScreen_(&core.playlistScreen())
    , playlistScreens_(&core.playlistScreens())
    , trackScreen_(&core.trackScreen())
    , settingsScreen_(&core.settingsScreen())
    , sourceSettings_(&core.sourceSettingsScreen())
    , pulse_(&core.pulseScreen())
    , updateScreen_(&core.updateScreen())
    , tracks_(&core.tracks())
    , sources_(&core.library().sourceRows())
    , favorites_(&core.library().favoriteRows())
    , otherPlaylists_(&core.library().otherRows())
    , downloads_(&core.downloadsScreen().jobRows())
    , openTracks_(&core.playlistScreen().rows())
    , openFolders_(&core.playlistScreen().folderRows())
    , openEntries_(&core.playlistScreen().entries())
    , trackPlaylists_(&core.trackScreen().playlistRows())
    , settingsSources_(&core.settingsScreen().sourceRows())
    , settingsProxies_(&core.settingsScreen().proxyRows())
    , sourceConnections_(&core.sourceSettingsScreen().connectionRows())
    , sourceFields_(&core.sourceSettingsScreen().fieldRows())
    , ready_(true)
{
    core_ = &core;
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
    acquire(library_, Remote::Names::library);
    acquireModel(sources_, Remote::Names::sources);
    acquireModel(favorites_, Remote::Names::favorites);
    acquireModel(otherPlaylists_, Remote::Names::otherPlaylists);
    acquire(mainScreen_, Remote::Names::mainScreen);
    acquire(downloadsScreen_, Remote::Names::downloadsScreen);
    acquireModel(downloads_, Remote::Names::downloads);
    acquire(playlistScreen_, Remote::Names::playlistScreen);
    acquire(playlistScreens_, Remote::Names::playlistScreens);
    acquireModel(openTracks_, Remote::Names::openTracks);
    acquireModel(openFolders_, Remote::Names::openFolders);
    acquireModel(openEntries_, Remote::Names::openEntries);
    acquire(trackScreen_, Remote::Names::trackScreen);
    acquireModel(trackPlaylists_, Remote::Names::trackPlaylists);
    acquire(settingsScreen_, Remote::Names::settingsScreen);
    acquireModel(settingsSources_, Remote::Names::settingsSources);
    acquireModel(settingsProxies_, Remote::Names::settingsProxies);
    acquire(sourceSettings_, Remote::Names::sourceSettings);
    acquireModel(sourceConnections_, Remote::Names::sourceConnections);
    acquireModel(sourceFields_, Remote::Names::sourceFields);
    acquire(pulse_, Remote::Names::pulse);
    acquire(updateScreen_, Remote::Names::updateScreen);

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
        delete playlistScreens_.data();
        for (const Level& level : std::as_const(levels_)) {
            delete level.screen.data();
            delete level.tracks.data();
            delete level.folders.data();
            delete level.entries.data();
        }
        delete openTracks_.data();
        delete openFolders_.data();
        delete openEntries_.data();
        delete trackScreen_.data();
        delete trackPlaylists_.data();
        delete settingsScreen_.data();
        delete settingsSources_.data();
        delete settingsProxies_.data();
        delete sourceSettings_.data();
        delete sourceConnections_.data();
        delete sourceFields_.data();
        delete pulse_.data();
        delete updateScreen_.data();
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

Backend::Level* Backend::level(int depth)
{
    if (depth <= 0 || depth >= Remote::Names::playlistLevels)
        return nullptr;
    if (auto it = levels_.find(depth); it != levels_.end())
        return &it.value();
    Level& level = levels_[depth];
#ifndef Q_OS_ANDROID
    if (core_ != nullptr) {
        ViewModel::PlaylistScreen& screen = core_->playlistScreens().at(depth);
        level.screen = &screen;
        level.tracks = &screen.rows();
        level.folders = &screen.folderRows();
        level.entries = &screen.entries();
        level.screenValid = true;
        level.modelsInitialized = 3;
        return &level;
    }
#endif
    if (!node_)
        return &level;
    // Not counted in `ready`: the app is usable before any page is that deep.
    const auto tell = [this, depth] {
        if (levels_.value(depth).ready()) {
            ++levelsRevision_;
            emit levelsRevisionChanged();
        }
    };
    QRemoteObjectDynamicReplica* screen
        = node_->acquireDynamic(Remote::Names::atDepth(Remote::Names::playlistScreen, depth));
    screen->setParent(this);
    level.screen = screen;
    connect(screen, &QRemoteObjectReplica::stateChanged, this,
        [this, depth, tell](QRemoteObjectReplica::State state) {
            levels_[depth].screenValid = state == QRemoteObjectReplica::Valid;
            tell();
        });
    for (QPointer<QAbstractItemModel>* slot : { &level.tracks, &level.folders, &level.entries }) {
        const QString name = slot == &level.tracks ? Remote::Names::atDepth(Remote::Names::openTracks, depth)
            : slot == &level.folders               ? Remote::Names::atDepth(Remote::Names::openFolders, depth)
                                                   : Remote::Names::atDepth(Remote::Names::openEntries, depth);
        auto* model = node_->acquireModel(name);
        model->setParent(this);
        *slot = model;
        connect(model, &QAbstractItemModelReplica::initialized, this, [this, depth, tell] {
            ++levels_[depth].modelsInitialized;
            tell();
        });
    }
    return &level;
}

namespace {
// Handed to QML by a method, an object without a parent would be the JS
// engine's to delete; these are the core's or this one's.
template <typename Object>
Object* kept(Object* object)
{
    if (object != nullptr)
        QJSEngine::setObjectOwnership(object, QJSEngine::CppOwnership);
    return object;
}
} // namespace

QObject* Backend::playlistScreenAt(int depth)
{
    if (depth <= 0)
        return kept(playlistScreen());
    const Level* found = ready_ ? level(depth) : nullptr;
    return kept(found != nullptr && found->ready() ? found->screen.data() : nullptr);
}

QAbstractItemModel* Backend::openTracksAt(int depth)
{
    if (depth <= 0)
        return kept(openTracks());
    const Level* found = ready_ ? level(depth) : nullptr;
    return kept(found != nullptr && found->ready() ? found->tracks.data() : nullptr);
}

QAbstractItemModel* Backend::openFoldersAt(int depth)
{
    if (depth <= 0)
        return kept(openFolders());
    const Level* found = ready_ ? level(depth) : nullptr;
    return kept(found != nullptr && found->ready() ? found->folders.data() : nullptr);
}

QAbstractItemModel* Backend::openEntriesAt(int depth)
{
    if (depth <= 0)
        return kept(openEntries());
    const Level* found = ready_ ? level(depth) : nullptr;
    return kept(found != nullptr && found->ready() ? found->entries.data() : nullptr);
}

void Backend::prepareLevel(int depth) { level(depth); }

void Backend::holdPulse()
{
    if (pulseHolders_++ == 0)
        tellPulseWatching();
}

void Backend::releasePulse()
{
    if (pulseHolders_ == 0 || --pulseHolders_ > 0)
        return;
    tellPulseWatching();
}

void Backend::tellPulseWatching()
{
    if (QObject* screen = pulse())
        QMetaObject::invokeMethod(screen, "setWatching", Q_ARG(bool, pulseHolders_ > 0));
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
        // A core that's new (its process restarted) knows nothing of the views.
        tellPulseWatching();
    } else {
        qCWarning(lcBackend) << "lost the core";
        restartTimer_.start();
    }
    emit readyChanged();
}

} // namespace Ui
