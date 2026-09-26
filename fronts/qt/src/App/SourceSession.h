#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "Coro.h"

namespace Covers {
class CoverArtCache;
}
namespace Library {
class TrackStates;
}
namespace Playback {
class PlaybackController;
}
namespace ViewModel {
class Messages;
}
namespace Rpc {
class AuthStates;
class RpcClient;
class SourceManager;
} // namespace Rpc

namespace App {

// A running source's side of the conversation that doesn't depend on any
// view: its notifications go where they belong (streams and radio tracks
// to playback, sign-in prompts and results to Rpc::AuthStates, errors to
// ViewModel::Messages), and it's signed in as it starts. Part of App::Core,
// so this keeps working while no window exists.
class SourceSession : public QObject {
    Q_OBJECT

public:
    SourceSession(Rpc::SourceManager& sourceManager, Playback::PlaybackController& playback,
        Rpc::AuthStates& authStates, Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache,
        ViewModel::Messages& messages, QObject* parent = nullptr);

    // Asks the source to sign in unless it already is (auth.getStatus, then
    // auth.start) — its prompt and result then come in as notifications.
    Rpc::Task<void> signIn(QString sourceId);
    // The user's answer to a usernamePassword prompt (auth.submit). An
    // empty result on success, else what went wrong (also posted as an
    // error message).
    Rpc::Task<QString> submitSignIn(QString sourceId, QJsonObject fields);

signals:
    // A source has started and is wired up — ready to list its playlists.
    void sourceReady(Rpc::RpcClient* client);
    // A source finished signing in — what it lists may have changed.
    void signedIn(Rpc::RpcClient* client);

private:
    void wire(Rpc::RpcClient* client);
    Rpc::Task<void> signIn(Rpc::RpcClient* client);

    Rpc::SourceManager& sourceManager_;
    Playback::PlaybackController& playback_;
    Rpc::AuthStates& authStates_;
    Library::TrackStates& trackStates_;
    Covers::CoverArtCache& coverArtCache_;
    ViewModel::Messages& messages_;
};

} // namespace App
