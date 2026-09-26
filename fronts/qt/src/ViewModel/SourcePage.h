#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

namespace App {
class SourceSession;
}

namespace ViewModel {

// A source's page, as far as signing in goes: asking it again (Retry /
// Sign in) and sending the user's answer to a username/password prompt —
// each busy while in flight, so the page can't be clicked twice. What the
// page shows otherwise comes straight from Rpc::AuthStates,
// ViewModel::Sources and the source's capabilities.
class SourcePage : public QObject {
    Q_OBJECT

public:
    explicit SourcePage(App::SourceSession& sourceSession, QObject* parent = nullptr);

    bool isBusy(const QString& sourceId) const { return busy_.contains(sourceId); }
    void signIn(const QString& sourceId);
    void submit(const QString& sourceId, const QJsonObject& fields);

signals:
    void busyChanged(const QString& sourceId, bool busy);
    // The source turned the submitted answer down — why, for the page.
    void submitFailed(const QString& sourceId, const QString& error);

private:
    void setBusy(const QString& sourceId, bool busy);

    App::SourceSession& sourceSession_;
    QSet<QString> busy_;
};

} // namespace ViewModel
