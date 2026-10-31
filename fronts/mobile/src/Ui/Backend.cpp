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
}

#ifndef Q_OS_ANDROID
Backend::Backend(App::MobileCore& core, QObject* parent)
    : QObject(parent)
    , nowPlaying_(&core.nowPlaying())
    , playlist_(&core.activePlaylist())
    , settings_(&core.settings())
    , tracks_(&core.activePlaylist().tracks())
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

    auto* tracks = node_->acquireModel(Remote::Names::tracks);
    tracks->setParent(this);
    tracks_ = tracks;
    ++pending_;
    connect(tracks, &QAbstractItemModelReplica::initialized, this, [this] {
        --pending_;
        checkReady();
    });
}

Backend::~Backend()
{
    // Replicas before their node: they unregister from it as they go.
    if (node_) {
        delete nowPlaying_.data();
        delete playlist_.data();
        delete settings_.data();
        delete tracks_.data();
    }
}

void Backend::acquire(QPointer<QObject>& slot, const QString& name)
{
    QRemoteObjectDynamicReplica* replica = node_->acquireDynamic(name);
    replica->setParent(this);
    slot = replica;
    ++pending_;
    // A dynamic replica only has its properties once the source has sent
    // them; QML binds to the meta-object it sees, so it must wait for this.
    connect(replica, &QRemoteObjectReplica::initialized, this, [this] {
        --pending_;
        checkReady();
    });
    connect(replica, &QRemoteObjectReplica::stateChanged, this,
        [name](QRemoteObjectReplica::State state) { qCInfo(lcBackend) << name << "replica:" << state; });
}

void Backend::checkReady()
{
    if (ready_ || pending_ > 0)
        return;
    ready_ = true;
    Logging::mark(QStringLiteral("core replicas ready"));
    emit readyChanged();
}

} // namespace Ui
