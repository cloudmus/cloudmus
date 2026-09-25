#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

#include "BackendManifest.h"
#include "RpcClient.h"

namespace Rpc {

// Owns one RpcClient per discovered backend manifest; restarts a crashed
// backend with backoff (2s/5s/10s, max 3 attempts) before giving up for the
// session, mirroring fronts/tui/cloudmus_tui/source_manager.py.
class SourceManager : public QObject {
    Q_OBJECT

public:
    explicit SourceManager(QObject* parent = nullptr);

    // Every backend manifest found (discoverManifests(), scanned once),
    // enabled or not.
    const QList<BackendManifest>& manifests();

    // The environment each backend process gets (Net::backendEnvironment()
    // — its proxy); the front's own environment without one.
    void setEnvironmentProvider(std::function<QProcessEnvironment(const QString& sourceId)> provider)
    {
        environmentProvider_ = std::move(provider);
    }

    // Backends the user switched off: never spawned, and not restarted.
    // Call before startAll().
    void setDisabledIds(const QStringList& ids);
    bool isEnabled(const QString& sourceId) const { return !disabledIds_.contains(sourceId); }

    // Spawns every enabled backend and starts its handshake. Safe to
    // call once at startup.
    void startAll();

    // Switches one backend on (spawned, with a fresh restart budget) or
    // off (shut down per docs/protocol.md §4.2; client() no longer returns
    // it) at runtime.
    void setEnabled(const QString& sourceId, bool enabled);

    // Its last start ended in sourceUnavailable() — rather than still
    // starting, or up and running.
    bool isUnavailable(const QString& sourceId) const { return unavailableIds_.contains(sourceId); }

    // Shuts a running backend down and spawns it again — for a setting it
    // only reads at startup (docs/protocol.md §7.7's restartRequired).
    void restart(const QString& sourceId);
    // A backend's own settings just changed (settings.update succeeded):
    // anything it lists may have too.
    void notifySettingsChanged(const QString& sourceId) { emit settingsChanged(sourceId); }

    QList<RpcClient*> clients() const;
    RpcClient* client(const QString& sourceId) const;

    void shutdownAll();

signals:
    // A backend is being spawned by startAll() or setEnabled(); its
    // sourceReady()/sourceUnavailable() follows. Not emitted for crash
    // restarts — to the UI, that's still the same running source.
    void sourceStarting(const Rpc::BackendManifest& manifest);
    // setEnabled(false) took a backend down.
    void sourceStopped(const QString& sourceId);
    void settingsChanged(const QString& sourceId);
    // Emitted once a client's initialize handshake completes successfully.
    void sourceReady(RpcClient* client);
    // Emitted when a source is unavailable and has exhausted its restart
    // attempts for this session (or failed its very first start).
    void sourceUnavailable(const QString& manifestId, const QString& name, QStringList stderrTail);

private:
    void startOne(const BackendManifest& manifest);
    void watch(RpcClient* client);

    static constexpr int kMaxRestartAttempts = 3;
    static constexpr int kRestartDelaysMs[3] = { 2000, 5000, 10000 };

    std::optional<QList<BackendManifest>> manifests_;
    std::function<QProcessEnvironment(const QString&)> environmentProvider_;
    QSet<QString> disabledIds_;
    QSet<QString> unavailableIds_;
    QHash<QString, RpcClient*> clientsById_;
    QHash<QString, int> restartAttempts_;
};

} // namespace Rpc
