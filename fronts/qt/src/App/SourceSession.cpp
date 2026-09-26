#include "SourceSession.h"

#include <QLoggingCategory>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "Messages.h"
#include "PlaybackController.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "SourceManager.h"
#include "TrackStates.h"

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcSourceSession, "cloudmus.app.sourcesession")
}

SourceSession::SourceSession(Rpc::SourceManager& sourceManager, Playback::PlaybackController& playback,
    Rpc::AuthStates& authStates, Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache,
    ViewModel::Messages& messages, QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , playback_(playback)
    , authStates_(authStates)
    , trackStates_(trackStates)
    , coverArtCache_(coverArtCache)
    , messages_(messages)
{
    connect(&sourceManager_, &Rpc::SourceManager::sourceReady, this, &SourceSession::wire);
}

void SourceSession::wire(Rpc::RpcClient* client)
{
    client->notifications.onTrackStreamReady
        = [this, client](const StreamReadyParams& p) { playback_.handleStreamReady(client->sourceId(), p); };
    client->notifications.onRadioTracksAdded = [this, client](const TracksAddedParams& p) {
        trackStates_.observe(client->sourceId(), p.tracks);
        coverArtCache_.assignSource(client->sourceId(), p.tracks);
        playback_.handleTracksAdded(client->sourceId(), p);
    };
    client->notifications.onError = [this](const ErrorParams& e) { messages_.error(e.message); };
    // Into authStates_, whose changed() repaints every view of it.
    client->onAuthPromptRaw
        = [this, client](const QJsonObject& params) { authStates_.setPrompt(client->sourceId(), params); };
    client->notifications.onAuthStatusChanged = [this, client](const StatusChangedParams& status) {
        if (status.status == QStringLiteral("authenticated")) {
            authStates_.setAuthenticated(client->sourceId());
            emit signedIn(client);
        } else {
            const QString message = status.message.value_or(QString());
            qCWarning(lcSourceSession) << "auth error for" << client->sourceId() << ":" << message;
            messages_.error(tr("%1: %2").arg(client->sourceName(), message));
            authStates_.setError(client->sourceId(), message);
        }
    };

    const QJsonObject auth = client->capabilities().value(QStringLiteral("auth")).toObject();
    if (auth.value(QStringLiteral("required")).toBool())
        signIn(client).detach();
    emit sourceReady(client);
}

Rpc::Task<void> SourceSession::signIn(QString sourceId)
{
    if (Rpc::RpcClient* client = sourceManager_.client(sourceId))
        co_await signIn(client);
}

Rpc::Task<void> SourceSession::signIn(Rpc::RpcClient* client)
{
    try {
        GetStatusResult status = co_await Rpc::authGetStatus(*client);
        if (status.status != QStringLiteral("authenticated")) {
            // auth.start is idempotent on the backend side (a session
            // already in flight just no-ops) — safe to call unconditionally
            // for unauthenticated/pending/error status alike, same as the
            // TUI's `if status["status"] != "authenticated": auth.start`.
            co_await Rpc::authStart(*client);
        }
    } catch (const std::exception& e) {
        // std::exception, not Rpc::RpcCallException: also catches
        // Rpc::ProtocolParseError (a well-formed JSON-RPC response whose
        // *content* doesn't match the protocol schema — e.g. a field typed
        // wrong) — same std::runtime_error base, e.what() carries the same
        // message either way (RpcCallException's constructor sets it from
        // error.message directly). A failure here is auth.getStatus/
        // auth.start itself erroring, distinct from the backend's own auth
        // flow later failing asynchronously via auth/statusChanged (handled
        // in wire()). Both must be visible: this used to only catch
        // RpcCallException and silently swallow anything else, which is
        // exactly how a Retry click could look like it did nothing.
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcSourceSession) << "auth.getStatus/auth.start failed for" << client->sourceId() << ":" << message;
        messages_.error(tr("%1: %2").arg(client->sourceName(), message));
    }
}

Rpc::Task<QString> SourceSession::submitSignIn(QString sourceId, QJsonObject fields)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return QString();
    try {
        SubmitParams params;
        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
            params.fields.insert(it.key(), it.value().toString());
        co_await Rpc::authSubmit(*client, params);
    } catch (const std::exception& e) {
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcSourceSession) << "auth.submit failed for" << sourceId << ":" << message;
        messages_.error(tr("%1: %2").arg(client->sourceName(), message));
        co_return message;
    }
    co_return QString();
}

} // namespace App
