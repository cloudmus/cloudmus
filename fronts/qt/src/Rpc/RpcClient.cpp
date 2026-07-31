#include "RpcClient.h"

#include <utility>

#include <QLoggingCategory>
#include <QTimer>

namespace Rpc {

namespace {

Q_LOGGING_CATEGORY(lcRpcClient, "cloudmus.rpc.client")

// Resolves to the raw JSON `result` object of one request, or throws
// RpcCallException on an `error` response or timeout. Not a template: every
// call funnels through this one primitive and typed results are parsed on
// top of it (see RpcClient::callWithId), so there's only ever one awaiter
// implementation to get right.
class CallAwaiter {
public:
    CallAwaiter(RpcClient& client, int id, int timeoutMs)
        : client_(client)
        , id_(id)
        , timeoutMs_(timeoutMs)
    {
    }

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        client_.registerPending(
            id_, timeoutMs_,
            [this, h](const QJsonObject& result) mutable {
                value_ = result;
                h.resume();
            },
            [this, h](RpcError err) mutable {
                error_ = std::move(err);
                h.resume();
            });
    }

    QJsonObject await_resume()
    {
        if (error_)
            throw RpcCallException(*error_);
        return value_.value_or(QJsonObject { });
    }

private:
    RpcClient& client_;
    int id_;
    int timeoutMs_;
    std::optional<QJsonObject> value_;
    std::optional<RpcError> error_;
};

} // namespace

RpcClient::RpcClient(BackendManifest manifest, QObject* parent)
    : QObject(parent)
    , manifest_(std::move(manifest))
{
    connect(&transport_, &NdjsonTransport::messageReceived, this, &RpcClient::handleMessage);
    auto onTransportDown = [this]() {
        if (!available_ && pending_.isEmpty())
            return; // never started, or already torn down
        available_ = false;
        failAllPending(RpcError { -1, QStringLiteral("backend disconnected"), { } });
        emit becameUnavailable();
    };
    connect(&transport_, &NdjsonTransport::finished, this,
            [this, onTransportDown](int exitCode, QProcess::ExitStatus status) {
                qCWarning(lcRpcClient) << manifest_.id << "process finished, exitCode=" << exitCode
                                       << "status=" << status;
                onTransportDown();
            });
    connect(&transport_, &NdjsonTransport::errorOccurred, this, [this, onTransportDown](QProcess::ProcessError error) {
        qCWarning(lcRpcClient) << manifest_.id << "process error:" << error;
        onTransportDown();
    });
    connect(&transport_, &NdjsonTransport::framingError, this, [this](const QString& rawLine) {
        qCWarning(lcRpcClient) << manifest_.id << "sent a non-JSON-RPC line on stdout (protocol violation):" << rawLine;
    });
    // Mirrors the backend's own stderr logging (e.g. yandex_music/requests
    // internals) live into the front's log, instead of it only surfacing
    // via stderrTail() when the process has already died — see Logging.h's
    // --debug/CLOUDMUS_QT_DEBUG gate, which is what actually silences this
    // by default. .noquote() because this is a preformatted line, not a
    // value QDebug should wrap in quotes.
    connect(&transport_, &NdjsonTransport::stderrLine, this, [this](const QString& line) {
        qCDebug(lcRpcClient).noquote() << QStringLiteral("[%1] %2").arg(manifest_.id, line);
    });
}

Task<void> RpcClient::start()
{
    transport_.start(manifest_.argv);

    const QJsonObject params {
        { QStringLiteral("protocolVersion"), QStringLiteral("1.1") },
        { QStringLiteral("front"),
          QJsonObject { { QStringLiteral("name"), QStringLiteral("cloudmus-qt") },
                        { QStringLiteral("version"), QStringLiteral("0.1.0") } } },
    };
    QJsonObject result = co_await callRaw(QStringLiteral("initialize"), params, 5000);

    const QJsonObject source = result.value(QStringLiteral("source")).toObject();
    sourceId_ = source.value(QStringLiteral("id")).toString();
    sourceName_ = source.value(QStringLiteral("name")).toString();
    capabilities_ = result.value(QStringLiteral("capabilities")).toObject();
    available_ = true;
}

Task<void> RpcClient::shutdown()
{
    if (!available_)
        co_return;
    try {
        co_await callVoid(QStringLiteral("shutdown"), { }, 3000);
    } catch (const RpcCallException&) {
        // best-effort — proceed to close the pipes regardless, see docs/protocol.md §4.2
    }
    available_ = false;
    transport_.closeStdin();
    transport_.terminateThenKill(2000);
}

int RpcClient::allocateRequestId() { return nextId_++; }

Task<QJsonObject> RpcClient::callRawWithId(int id, QString method, QJsonObject params, int timeoutMs)
{
    const QJsonObject request {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), id },
        { QStringLiteral("method"), method },
        { QStringLiteral("params"), params },
    };
    transport_.writeMessage(request);
    co_return co_await CallAwaiter(*this, id, timeoutMs);
}

void RpcClient::registerPending(int id, int timeoutMs, std::function<void(const QJsonObject&)> onResult,
                                std::function<void(RpcError)> onError)
{
    pending_.insert(id, PendingEntry { std::move(onResult), std::move(onError) });
    QTimer::singleShot(timeoutMs, this, [this, id]() {
        auto it = pending_.find(id);
        if (it == pending_.end())
            return; // already resolved before the timeout fired
        PendingEntry entry = it.value();
        pending_.erase(it);
        entry.onError(RpcError { -1, QStringLiteral("request timed out"), { } });
    });
}

void RpcClient::handleMessage(const QJsonObject& msg)
{
    if (msg.contains(QStringLiteral("method"))) {
        // Sources never send requests to the front (docs/protocol.md §3) —
        // a message with `method` and no response fields is a notification.
        dispatchNotification(msg.value(QStringLiteral("method")).toString(),
                             msg.value(QStringLiteral("params")).toObject());
        return;
    }
    if (!msg.contains(QStringLiteral("id")))
        return;
    const int id = msg.value(QStringLiteral("id")).toInt();
    auto it = pending_.find(id);
    if (it == pending_.end())
        return; // late/unknown response id — discard
    PendingEntry entry = it.value();
    pending_.erase(it);

    if (msg.contains(QStringLiteral("error"))) {
        const QJsonObject errObj = msg.value(QStringLiteral("error")).toObject();
        entry.onError(RpcError {
            errObj.value(QStringLiteral("code")).toInt(),
            errObj.value(QStringLiteral("message")).toString(),
            errObj.value(QStringLiteral("data")).toObject(),
        });
    } else {
        entry.onResult(msg.value(QStringLiteral("result")).toObject());
    }
}

void RpcClient::dispatchNotification(const QString& method, const QJsonObject& params)
{
    if (method == QStringLiteral("state/changed")) {
        if (notifications.onStateChanged)
            notifications.onStateChanged(PlaybackState::fromJson(params));
    } else if (method == QStringLiteral("track/streamReady")) {
        if (notifications.onTrackStreamReady)
            notifications.onTrackStreamReady(StreamReadyParams::fromJson(params));
    } else if (method == QStringLiteral("radio/tracksAdded")) {
        if (notifications.onRadioTracksAdded)
            notifications.onRadioTracksAdded(TracksAddedParams::fromJson(params));
    } else if (method == QStringLiteral("auth/prompt")) {
        if (onAuthPromptRaw)
            onAuthPromptRaw(params); // see the member's doc comment for why this bypasses `notifications`
    } else if (method == QStringLiteral("auth/statusChanged")) {
        if (notifications.onAuthStatusChanged)
            notifications.onAuthStatusChanged(StatusChangedParams::fromJson(params));
    } else if (method == QStringLiteral("error")) {
        if (notifications.onError)
            notifications.onError(ErrorParams::fromJson(params));
    }
    // Unknown notification: ignore — forward-compatible with a newer minor
    // protocol version, same tolerant-parsing precedent as the Python side.
}

void RpcClient::failAllPending(const RpcError& err)
{
    const auto pending = std::exchange(pending_, { });
    for (const auto& entry : pending) {
        entry.onError(err);
    }
}

} // namespace Rpc
