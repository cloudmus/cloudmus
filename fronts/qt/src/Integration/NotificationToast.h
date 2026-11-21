#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

#include <optional>
#ifdef Q_OS_MACOS
#include <QTemporaryDir>

#include <memory>
#endif

class QSystemTrayIcon;
class QTimer;

namespace Integration {

// One OS-level org.freedesktop.Notifications popup per track change,
// carrying the cover art — this is what actually satisfies "hint with
// track name + cover" (QSystemTrayIcon::setToolTip() is plain-text only on
// Linux, see TrayIcon). Reuses the same notification id on every call so
// repeated notifications update in place instead of stacking.
class NotificationToast : public QObject {
    Q_OBJECT

public:
    explicit NotificationToast(QSystemTrayIcon* trayIcon = nullptr, QObject* parent = nullptr);

    void showTrackChange(const QString& title, const QString& artist, const QPixmap& cover);
    // A short one about what a hotkey did ("Liked", "Volume 65%"), apart
    // from the track's: pressing the key again updates it in place.
    void showNotice(const QString& title, const QString& body, const QPixmap& cover);
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
    // Also not replaced once its own timeout has passed, even if no
    // NotificationClosed came: Plasma doesn't always send one for a popup
    // that expired, and replacing that one stays silent.
    struct Request {
        QString title;
        QString body;
        QPixmap cover;
        int timeoutMs = 0;
    };
    struct Slot {
        uint id = 0;
        QElapsedTimer sentAt;
        // Notify is out and its id not back yet. What is asked for
        // meanwhile waits (the latest only), to replace that one in place
        // rather than pop up beside it.
        bool pending = false;
        std::optional<Request> queued;
    };
    Slot track_;
    Slot notice_;
    // Every notification of ours still around (to tell our clicks from
    // other apps' — the signals are broadcast), and the activation token
    // the server sent for the one about to be clicked.
    QSet<uint> ownIds_;
    QString pendingActivationToken_;
    QSystemTrayIcon* trayIcon_ = nullptr;
#ifndef Q_OS_WIN
    // Sends one notification, replacing the slot's one if it is still up.
    // Never waits for the server: it is plasmashell on KDE, which at login
    // may itself be waiting for somebody who waits for us.
    void send(Slot& slot, const Request& request);
#endif
#ifdef Q_OS_MACOS
    // macOS: UserNotifications, see NotificationToastMac.mm. The center's
    // delegate (an Objective-C object, released with this), and the covers
    // handed to it as files.
    std::shared_ptr<void> delegate_;
    QTemporaryDir coverDir_;
    quint64 coverSerial_ = 0;
#endif
#ifdef Q_OS_WIN
    // Windows: sends the tray balloon itself, see NotificationToastWin.cpp.
    void showBalloon(const QString& title, const QString& artist, const QPixmap& cover);
    // A track whose cover is still loading, waiting a moment for it —
    // a balloon can't be updated in place, only replaced by a second one.
    QTimer* coverWait_ = nullptr;
    QString waitingTitle_;
    QString waitingArtist_;
    void* balloonIcon_ = nullptr; // HICON, kept until the next balloon
#endif
};

} // namespace Integration
