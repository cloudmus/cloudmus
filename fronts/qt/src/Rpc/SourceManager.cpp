#include "SourceManager.h"

#include <QLoggingCategory>
#include <QTimer>

namespace Rpc {

namespace {

Q_LOGGING_CATEGORY(lcSourceManager, "cloudmus.rpc.sourcemanager")

// Runs client->start(), reporting success/failure back through the two
// SourceManager signals. A free coroutine (not a SourceManager method) so
// it can be detach()ed cleanly from startOne() without SourceManager itself
// needing to be a coroutine.
Task<void> runStart(SourceManager& manager, RpcClient* client)
{
    // Switched off (SourceManager::setEnabled) while still handshaking:
    // its shutdown() was a no-op then, as nothing was up yet.
    const auto superseded = [&manager, client]() { return manager.client(client->manifest().id) != client; };
    try {
        co_await client->start();
        if (superseded()) {
            co_await client->shutdown();
            co_return;
        }
        qCInfo(lcSourceManager) << client->manifest().id << "connected —" << client->sourceName()
                                << "capabilities=" << client->capabilities();
        emit manager.sourceReady(client);
    } catch (const RpcCallException& e) {
        if (superseded())
            co_return;
        qCWarning(lcSourceManager) << client->manifest().id << "failed to start:" << e.error().message
                                   << "stderr tail:" << client->stderrTail();
        emit manager.sourceUnavailable(client->manifest().id, client->manifest().name, client->stderrTail());
    }
}

} // namespace

SourceManager::SourceManager(QObject* parent)
    : QObject(parent)
{
    connect(this, &SourceManager::sourceUnavailable, this,
        [this](const QString& manifestId) { unavailableIds_.insert(manifestId); });
}

const QList<BackendManifest>& SourceManager::manifests()
{
    if (!manifests_)
        manifests_ = discoverManifests();
    return *manifests_;
}

void SourceManager::setDisabledIds(const QStringList& ids) { disabledIds_ = QSet<QString>(ids.begin(), ids.end()); }

void SourceManager::startAll()
{
    for (const auto& manifest : manifests()) {
        if (!isEnabled(manifest.id))
            continue;
        emit sourceStarting(manifest);
        startOne(manifest);
    }
}

void SourceManager::setEnabled(const QString& sourceId, bool enabled)
{
    if (enabled == isEnabled(sourceId))
        return;
    if (enabled) {
        disabledIds_.remove(sourceId);
        for (const auto& manifest : manifests()) {
            if (manifest.id == sourceId) {
                restartAttempts_.remove(sourceId);
                emit sourceStarting(manifest);
                startOne(manifest);
            }
        }
        return;
    }

    disabledIds_.insert(sourceId);
    RpcClient* client = clientsById_.take(sourceId);
    if (client == nullptr)
        return;
    // Before shutdown(): a request still in flight makes the process exit
    // look like a crash (becameUnavailable), and watch()'s handler would
    // restart it. The client object itself stays parented to `this`, like
    // a crashed one replaced by a restart: coroutines may still hold it.
    disconnect(client, nullptr, this, nullptr);
    client->shutdown().detach();
    qCInfo(lcSourceManager) << sourceId << "disabled";
    emit sourceStopped(sourceId);
}

void SourceManager::startOne(const BackendManifest& manifest)
{
    qCDebug(lcSourceManager) << "starting" << manifest.id << manifest.argv;
    unavailableIds_.remove(manifest.id);
    auto* client = new RpcClient(manifest, this);
    clientsById_.insert(manifest.id, client);
    watch(client);
    runStart(*this, client).detach();
}

void SourceManager::watch(RpcClient* client)
{
    const QString id = client->manifest().id;
    connect(client, &RpcClient::becameUnavailable, this, [this, client, id]() {
        int attempt = restartAttempts_.value(id, 0);
        if (attempt >= kMaxRestartAttempts) {
            qCWarning(lcSourceManager) << id << "exhausted" << kMaxRestartAttempts
                                       << "restart attempts, giving up for this session";
            emit sourceUnavailable(id, client->manifest().name, client->stderrTail());
            return;
        }
        const int delayMs = kRestartDelaysMs[attempt];
        qCWarning(lcSourceManager) << id << "became unavailable, retrying in" << delayMs << "ms (attempt"
                                   << (attempt + 1) << "of" << kMaxRestartAttempts << ")";
        restartAttempts_.insert(id, attempt + 1);
        const BackendManifest manifest = client->manifest();
        QTimer::singleShot(delayMs, this, [this, manifest]() {
            // Switched off while waiting to restart.
            if (!isEnabled(manifest.id))
                return;
            // The old client object stays parented to `this` and gets
            // cleaned up with it; replace the map entry with a fresh client.
            startOne(manifest);
        });
    });
}

QList<RpcClient*> SourceManager::clients() const { return clientsById_.values(); }

RpcClient* SourceManager::client(const QString& sourceId) const { return clientsById_.value(sourceId, nullptr); }

void SourceManager::shutdownAll()
{
    for (RpcClient* client : std::as_const(clientsById_)) {
        client->shutdown().detach();
    }
}

} // namespace Rpc
