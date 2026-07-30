#pragma once

#include <QObject>
#include <QPixmap>
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

private:
    uint lastNotificationId_ = 0;
};

} // namespace Integration
