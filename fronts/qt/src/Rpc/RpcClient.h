#pragma once

#include <functional>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <stdexcept>

#include "BackendManifest.h"
#include "Coro.h"
#include "NdjsonTransport.h"
#include "RpcMethods.h"

namespace Rpc {

struct RpcError {
    int code = 0;
    QString message;
    QJsonObject data;
};

class RpcCallException : public std::runtime_error {
public:
    explicit RpcCallException(RpcError error)
        : std::runtime_error(error.message.toStdString())
        , error_(std::move(error))
    {
    }
    const RpcError& error() const { return error_; }

private:
    RpcError error_;
};

// Owns one backend subprocess: spawns it, drives the initialize/shutdown
// handshake, and provides the request/response correlation coroutines that
// generated/RpcMethods.h's typed wrappers (and a few hand-written callers
// that need the raw JSON, like initialize itself) build on. See
// docs/protocol.md §4-§5 and fronts/tui/cloudmus_tui/rpc_client.py for the
// Python front's equivalent this mirrors.
class RpcClient : public QObject {
    Q_OBJECT

public:
    explicit RpcClient(BackendManifest manifest, QObject* parent = nullptr);

    Task<void> start();
    Task<void> shutdown();

    // Low-level primitives generated call-wrapper functions (and start())
    // build on. `id` is allocated by the caller via allocateRequestId() —
    // needed by PlaybackController for the track/streamReady race (see
    // docs/protocol.md §11.1): it must know the request id *before*
    // awaiting the response, to record it as the latest in-flight play.
    //
    // method/params are taken BY VALUE, not by reference: Task's initial
    // suspend is lazy (suspend_always), so a coroutine's body may not start
    // running until long after the function that constructed it returned —
    // e.g. call()/callVoid() below are plain (non-coroutine) functions that
    // just forward into a lazily-suspended coroutine and hand the Task back
    // up to a caller that only awaits it later. A reference parameter would
    // dangle the moment any temporary bound to it (a QStringLiteral, a
    // `.toJson()` result, ...) is destroyed at the end of that outer full
    // expression — QString/QJsonObject are implicitly shared, so by-value
    // here is just a refcount bump, not a real copy.
    int allocateRequestId();
    Task<QJsonObject> callRawWithId(int id, QString method, QJsonObject params, int timeoutMs);
    Task<QJsonObject> callRaw(QString method, QJsonObject params, int timeoutMs)
    {
        return callRawWithId(allocateRequestId(), std::move(method), std::move(params), timeoutMs);
    }

    template <typename Result>
    Task<Result> callWithId(int id, QString method, QJsonObject params, int timeoutMs)
    {
        QJsonObject raw = co_await callRawWithId(id, std::move(method), std::move(params), timeoutMs);
        co_return Result::fromJson(raw);
    }
    Task<void> callVoidWithId(int id, QString method, QJsonObject params, int timeoutMs)
    {
        co_await callRawWithId(id, std::move(method), std::move(params), timeoutMs);
    }

    template <typename Result>
    Task<Result> call(QString method, QJsonObject params, int timeoutMs)
    {
        return callWithId<Result>(allocateRequestId(), std::move(method), std::move(params), timeoutMs);
    }
    Task<void> callVoid(QString method, QJsonObject params, int timeoutMs)
    {
        return callVoidWithId(allocateRequestId(), std::move(method), std::move(params), timeoutMs);
    }

    // Only registerPending() needs to be callable from outside (the
    // CallAwaiter in RpcClient.cpp) — kept public rather than friended
    // since it's a small, self-contained primitive.
    void registerPending(int id, int timeoutMs, std::function<void(const QJsonObject&)> onResult,
                         std::function<void(RpcError)> onError);

    bool available() const { return available_; }
    const QJsonObject& capabilities() const { return capabilities_; }
    const QString& sourceId() const { return sourceId_; }
    const QString& sourceName() const { return sourceName_; }
    // Optional (protocol 1.2+) — empty if the source didn't supply one.
    const QString& sourceDescription() const { return sourceDescription_; }
    const BackendManifest& manifest() const { return manifest_; }
    QStringList stderrTail() const { return transport_.stderrTail(); }

    // Filled in by whoever owns this client (SourceManager/PlaybackController)
    // to receive typed notification pushes.
    NotificationDispatch notifications;

    // auth/prompt's payload varies per capabilities.auth.flow (deviceCode
    // carries url/code/expiresInSec; usernamePassword carries fields;
    // oauthRedirect carries url) and protocol/methods.yaml deliberately
    // doesn't model it as a oneOf (see the comment there) — so it's
    // dispatched here as raw JSON instead of through `notifications`,
    // mirroring the same call in cloudmus_backend_yandex/auth.py.
    std::function<void(const QJsonObject&)> onAuthPromptRaw;

signals:
    void becameUnavailable();

private:
    void handleMessage(const QJsonObject& msg);
    void dispatchNotification(const QString& method, const QJsonObject& params);
    void failAllPending(const RpcError& err);

    struct PendingEntry {
        std::function<void(const QJsonObject&)> onResult;
        std::function<void(RpcError)> onError;
    };

    BackendManifest manifest_;
    NdjsonTransport transport_;
    int nextId_ = 1;
    QHash<int, PendingEntry> pending_;
    bool available_ = false;
    QString sourceId_;
    QString sourceName_;
    QString sourceDescription_;
    QJsonObject capabilities_;
};

} // namespace Rpc
