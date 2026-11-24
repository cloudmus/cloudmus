#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace App {
class Connectivity;
}
namespace Playback {
class PlaybackController;
}
namespace Rpc {
class SourceManager;
}

namespace ViewModel {

// The offline indicator, for both fronts: whether the system has a network
// and which services can't be reached (App::Connectivity), a line to show and
// the details for a tooltip or sheet, the actions waiting to be sent, and the
// click that checks again.
class ConnectionStatus : public QObject {
    Q_OBJECT
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString details READ details NOTIFY changed)
    Q_PROPERTY(QStringList unreachable READ unreachable NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(int pendingActions READ pendingActions NOTIFY changed)

public:
    enum State {
        Online,
        NoNetwork, // the system says so
        Unreachable, // some services, with a network
    };
    Q_ENUM(State)

    ConnectionStatus(App::Connectivity& connectivity, Rpc::SourceManager& sourceManager,
        Playback::PlaybackController& playback, QObject* parent = nullptr);

    int state() const;
    // Something to show: not all online.
    bool visible() const { return state() != Online; }
    // "No network", "Yandex Music is unreachable", ...
    QString text() const;
    // A line per service: its name, since when, why; and what waits to be sent.
    QString details() const;
    // The names of the services that can't be reached.
    QStringList unreachable() const;
    bool checking() const;
    int pendingActions() const { return pendingActions_; }
    // The outbox's count (App::Outbox), for the details.
    void setPendingActions(int count);

public slots:
    // The indicator's click: every service, now.
    void checkNow();
    // Whether anyone sees the app (a phone's app in the foreground): checks
    // stop while nobody does and nothing plays.
    void setViewShown(bool shown);

signals:
    void changed();
    void checkingChanged();

private:
    QString nameOf(const QString& sourceId) const;
    void applyPause();

    App::Connectivity& connectivity_;
    Rpc::SourceManager& sourceManager_;
    Playback::PlaybackController& playback_;
    int pendingActions_ = 0;
    bool viewShown_ = true;
};

} // namespace ViewModel
