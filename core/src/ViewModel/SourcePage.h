#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "Coro.h"

namespace App {
class SourceSession;
}

namespace ViewModel {

// A source's page, as far as signing in goes: asking it again (Retry /
// Sign in) and sending what came of its sign-in form or browser —
// each busy while in flight, so the page can't be clicked twice. What the
// page shows otherwise comes straight from Rpc::AuthStates,
// ViewModel::Sources and the source's capabilities.
class SourcePage : public QObject {
    Q_OBJECT

public:
    explicit SourcePage(App::SourceSession& sourceSession, QObject* parent = nullptr);

    bool isBusy(const QString& sourceId) const { return busy_.contains(sourceId); }
    void signIn(const QString& sourceId);
    // A sign-in form's values, by field name.
    void submit(const QString& sourceId, const QJsonObject& fields);
    // What a built-in browser ended up with (auth.submit's `browser`).
    void submitBrowser(const QString& sourceId, const QJsonObject& result);
    // See App::SourceSession::wake().
    bool wake(const QString& sourceId);
    bool canSignOut(const QString& sourceId) const;
    QJsonObject capabilities(const QString& sourceId) const;
    // What it can do as ids for a page to word ("radio", "download", ...), in
    // the order to list them; empty while the source isn't running.
    QStringList capabilityIds(const QString& sourceId) const;
    void signOut(const QString& sourceId);

signals:
    void busyChanged(const QString& sourceId, bool busy);
    // The source turned the submitted answer down — why, for the page.
    void submitFailed(const QString& sourceId, const QString& error);

private:
    void setBusy(const QString& sourceId, bool busy);
    // Busy until `submission` answers, telling why if it was turned down.
    void follow(const QString& sourceId, Rpc::Task<QString> submission);

    App::SourceSession& sourceSession_;
    QSet<QString> busy_;
};

} // namespace ViewModel
