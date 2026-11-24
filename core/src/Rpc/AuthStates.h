#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

namespace Rpc {

// What the front knows about each source's sign-in, kept here because
// nothing on RpcClient persists it: auth/prompt and auth/statusChanged are
// fire-and-forget pushes (MainWindow::wireSource() feeds them in), and
// auth.logout answers with no notification at all. Shared by everything
// that shows sign-in — the sidebar's warning icon, the source's page, the
// source's Settings page — so each follows the same state live.
class AuthStates : public QObject {
    Q_OBJECT

public:
    struct State {
        bool hasProblem = false; // capabilities.auth.required && not authenticated
        QJsonObject prompt; // last auth/prompt payload; empty if none yet
        QString errorMessage; // last auth/statusChanged error message; empty if none
    };

    using QObject::QObject;

    State state(const QString& sourceId) const { return states_.value(sourceId); }

    void setPrompt(const QString& sourceId, const QJsonObject& prompt);
    void setAuthenticated(const QString& sourceId);
    void setError(const QString& sourceId, const QString& message);
    // After a successful auth.logout: not signed in, nothing to act on
    // until the user asks to sign in again.
    void setSignedOut(const QString& sourceId);
    // The source went away (switched off): forget it.
    void remove(const QString& sourceId);

signals:
    void changed(const QString& sourceId);
    void signedOut(const QString& sourceId);
    // For statistics, the steps of a sign-in: asked to sign in (once until
    // it ends), then signed in after being asked, or an error.
    void signInPrompted(const QString& sourceId);
    void signInCompleted(const QString& sourceId);
    void signInFailed(const QString& sourceId);

private:
    QHash<QString, State> states_;
    QSet<QString> prompted_; // a sign-in under way
};

} // namespace Rpc
