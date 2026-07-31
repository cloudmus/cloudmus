#include "NotificationToast.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QImage>
#include <QVariantMap>

namespace Integration {

namespace {
// (iiibiiay): width, height, rowstride, has_alpha, bits_per_sample,
// channels, image data — the freedesktop.org Notifications "image-data"
// hint format.
QDBusArgument& operator<<(QDBusArgument& arg, const QImage& image)
{
    arg.beginStructure();
    arg << image.width() << image.height() << static_cast<int>(image.bytesPerLine()) << true << 8 << 4
        << QByteArray(reinterpret_cast<const char*>(image.constBits()), static_cast<int>(image.sizeInBytes()));
    arg.endStructure();
    return arg;
}
} // namespace

NotificationToast::NotificationToast(QObject* parent)
    : QObject(parent)
{
}

void NotificationToast::showTrackChange(const QString& title, const QString& artist, const QPixmap& cover)
{
    QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                         QStringLiteral("/org/freedesktop/Notifications"),
                         QStringLiteral("org.freedesktop.Notifications"), QDBusConnection::sessionBus());
    if (!iface.isValid())
        return;

    QVariantMap hints;
    if (!cover.isNull()) {
        const QImage image = cover.toImage().convertToFormat(QImage::Format_RGBA8888);
        QDBusArgument arg;
        arg << image;
        hints.insert(QStringLiteral("image-data"), QVariant::fromValue(arg));
    }

    QDBusReply<uint> reply = iface.call(QStringLiteral("Notify"), QStringLiteral("CloudMus"), lastNotificationId_,
                                        QString(), title, artist, QStringList(), hints, 5000);
    if (reply.isValid()) {
        lastNotificationId_ = reply.value();
    }
}

} // namespace Integration
