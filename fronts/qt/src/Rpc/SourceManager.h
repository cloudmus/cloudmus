#pragma once

#include <QHash>
#include <QObject>
#include <QString>

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

    // Spawns every discovered backend and starts its handshake. Safe to
    // call once at startup.
    void startAll();

    QList<RpcClient*> clients() const;
    RpcClient* client(const QString& sourceId) const;

    void shutdownAll();

signals:
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

    QHash<QString, RpcClient*> clientsById_;
    QHash<QString, int> restartAttempts_;
};

} // namespace Rpc
