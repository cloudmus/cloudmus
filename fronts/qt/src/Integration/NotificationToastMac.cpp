#include "NotificationToast.h"

namespace Integration {

// Not on macOS yet: its notifications (UserNotifications) need a signed
// app bundle, which comes with the packaging. Until then the app shows
// none of its own, as on a Linux desktop without a notification server.

NotificationToast::NotificationToast(QSystemTrayIcon* trayIcon, QObject* parent)
    : QObject(parent)
    , trayIcon_(trayIcon)
{
}

void NotificationToast::showTrackChange(const QString&, const QString&, const QPixmap&) { }
void NotificationToast::showNotice(const QString&, const QString&, const QPixmap&) { }
void NotificationToast::updateCover(const QString&, const QString&, const QPixmap&) { }

void NotificationToast::onNotificationClosed(uint, uint) { }
void NotificationToast::onActionInvoked(uint, const QString&) { }
void NotificationToast::onActivationToken(uint, const QString&) { }

} // namespace Integration
