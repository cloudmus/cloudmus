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
    try {
        co_await client->start();
        qCInfo(lcSourceManager) << client->manifest().id << "connected —" << client->sourceName()
                                << "capabilities=" << client->capabilities();
        emit manager.sourceReady(client);
    } catch (const RpcCallException& e) {
        qCWarning(lcSourceManager) << client->manifest().id << "failed to start:" << e.error().message
                                   << "stderr tail:" << client->stderrTail();
        emit manager.sourceUnavailable(client->manifest().id, client->manifest().name, client->stderrTail());
    }
}

} // namespace

SourceManager::SourceManager(QObject* parent)
    : QObject(parent)
{
}

void SourceManager::startAll()
{
    const auto manifests = discoverManifests();
    for (const auto& manifest : manifests) {
        startOne(manifest);
    }
}

void SourceManager::startOne(const BackendManifest& manifest)
{
    qCDebug(lcSourceManager) << "starting" << manifest.id << manifest.argv;
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
