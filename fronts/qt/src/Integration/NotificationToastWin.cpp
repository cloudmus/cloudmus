#include "NotificationToast.h"

#include <QSystemTrayIcon>

namespace Integration {

NotificationToast::NotificationToast(QSystemTrayIcon* trayIcon, QObject* parent)
    : QObject(parent)
    , trayIcon_(trayIcon)
{
    if (trayIcon_)
        connect(trayIcon_, &QSystemTrayIcon::messageClicked, this,
            [this]() { emit activated(QString()); });
}

void NotificationToast::showTrackChange(const QString& title, const QString& artist, const QPixmap& cover)
{
    Q_UNUSED(cover);
    if (trayIcon_ && trayIcon_->isVisible())
        trayIcon_->showMessage(title, artist, QSystemTrayIcon::Information, 5000);
}

void NotificationToast::updateCover(const QString&, const QString&, const QPixmap&) {}
void NotificationToast::onNotificationClosed(uint, uint) {}
void NotificationToast::onActionInvoked(uint, const QString&) {}
void NotificationToast::onActivationToken(uint, const QString&) {}

} // namespace Integration

#include "moc_NotificationToast.cpp"
