#pragma once

#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

namespace Integration {

// One OS-level org.freedesktop.Notifications popup per track change,
// carrying the cover art — this is what actually satisfies "hint with
// track name + cover" (QSystemTrayIcon::setToolTip() is plain-text only on
// Linux, see TrayIcon). Reuses the same notification id on every call so
// repeated notifications update in place instead of stacking.
class NotificationToast : public QObject {
    Q_OBJECT

public:
    explicit NotificationToast(QObject* parent = nullptr);

    void showTrackChange(const QString& title, const QString& artist, const QPixmap& cover);
    // Re-sends the notification still on screen with `cover` (e.g. once a
    // cover that was still loading arrives) — a no-op once it has closed,
    // so a late cover never pops up a second notification.
    void updateCover(const QString& title, const QString& artist, const QPixmap& cover);

signals:
    // One of our notifications was clicked. `activationToken` (may be
    // empty) lets the window be raised on Wayland, which only allows
    // focus-stealing with a token from the compositor — see main.cpp.
    void activated(const QString& activationToken);

private slots:
    // org.freedesktop.Notifications' signals.
    void onNotificationClosed(uint id, uint reason);
    void onActionInvoked(uint id, const QString& actionKey);
    void onActivationToken(uint id, const QString& token);

private:
    // The still-open notification a new track replaces in place (so fast
    // skipping doesn't stack popups). Reset once it closes: replacing a
    // notification that already expired only updates its entry in the
    // server's history (e.g. KDE Plasma) without popping up again — which
    // made every track after the first one silent.
    uint lastNotificationId_ = 0;
    // Every notification of ours still around (to tell our clicks from
    // other apps' — the signals are broadcast), and the activation token
    // the server sent for the one about to be clicked.
    QSet<uint> ownIds_;
    QString pendingActivationToken_;
};

} // namespace Integration
