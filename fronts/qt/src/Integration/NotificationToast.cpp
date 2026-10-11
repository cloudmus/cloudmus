#include "NotificationToast.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QStandardPaths>
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
// The app icon for the notification's header, as an icon-theme NAME.
// Not a file path: a path in app_icon is taken by the server (KDE Plasma)
// as the notification's image, replacing the track's cover (image-data).
// The name resolves through the user's hicolor theme — where the AppImage
// installs it (see AppRun). A build run straight from the repo has no
// such install, so the logo is placed there under the same name once.
QString appIconName()
{
    static const QString name = [] {
        const QString iconName = QStringLiteral("cloudmus-qt");
        const QString installed = QStandardPaths::locate(
            QStandardPaths::GenericDataLocation, QStringLiteral("icons/hicolor/512x512/apps/cloudmus-qt.png"));
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/icons/hicolor/scalable/apps");
        const QString svg = dir + QStringLiteral("/cloudmus-qt.svg");
        if (installed.isEmpty() && !QFile::exists(svg)) {
            QDir().mkpath(dir);
            QFile::copy(QStringLiteral(":/icons/icons/logo.svg"), svg);
            QFile::setPermissions(svg,
                QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        }
        return iconName;
    }();
    return name;
}
} // namespace

NotificationToast::NotificationToast(QSystemTrayIcon* trayIcon, QObject* parent)
    : QObject(parent)
{
    Q_UNUSED(trayIcon);
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"), QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("NotificationClosed"), this, SLOT(onNotificationClosed(uint, uint)));
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"), QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("ActionInvoked"), this, SLOT(onActionInvoked(uint, QString)));
    // Sent right before ActionInvoked by servers that support it (spec 1.2).
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"), QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("ActivationToken"), this, SLOT(onActivationToken(uint, QString)));
}

void NotificationToast::onActivationToken(uint id, const QString& token)
{
    if (ownIds_.contains(id))
        pendingActivationToken_ = token;
}

void NotificationToast::onActionInvoked(uint id, const QString& actionKey)
{
    if (!ownIds_.contains(id) || actionKey != QStringLiteral("default"))
        return;
    emit activated(pendingActivationToken_);
    pendingActivationToken_.clear();
}

void NotificationToast::onNotificationClosed(uint id, uint reason)
{
    Q_UNUSED(reason); // expired, dismissed or closed — gone from the screen either way
    ownIds_.remove(id);
    if (id == track_.id)
        track_.id = 0;
    if (id == notice_.id)
        notice_.id = 0;
}

void NotificationToast::updateCover(const QString& title, const QString& artist, const QPixmap& cover)
{
    // Same timeout as showTrackChange() — past it a late cover must not pop up again.
    if (track_.pending || (track_.id != 0 && track_.sentAt.isValid() && !track_.sentAt.hasExpired(5000)))
        showTrackChange(title, artist, cover);
}

void NotificationToast::showTrackChange(const QString& title, const QString& artist, const QPixmap& cover)
{
    send(track_, { title, artist, cover, 5000 });
}

void NotificationToast::showNotice(const QString& title, const QString& body, const QPixmap& cover)
{
    send(notice_, { title, body, cover, 2500 });
}

void NotificationToast::send(Slot& slot, const Request& request)
{
    if (slot.pending) {
        slot.queued = request;
        return;
    }
    if (slot.id != 0 && (!slot.sentAt.isValid() || slot.sentAt.hasExpired(request.timeoutMs)))
        slot.id = 0; // gone from the screen by now: a fresh popup, not a silent update

    QVariantMap hints;
    // Lets the server take the icon/name from the installed .desktop file
    // (the AppImage installs one — see AppRun); app_icon below names the
    // same icon for when there is none.
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("cloudmus-qt"));
    if (!request.cover.isNull()) {
        const QImage image = request.cover.toImage().convertToFormat(QImage::Format_RGBA8888);
        QDBusArgument arg;
        arg << image;
        hints.insert(QStringLiteral("image-data"), QVariant::fromValue(arg));
    }

    // A plain message rather than a QDBusInterface: creating one asks the
    // server to introspect itself and waits for the answer.
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"), QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("Notify"));
    message << QStringLiteral("CloudMus") << slot.id << appIconName() << request.title
            << request.body
            // "default": the action for clicking the notification itself.
            << QStringList { QStringLiteral("default"), tr("Show player") } << hints << request.timeoutMs;
    slot.pending = true;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    // The slots are members: they live as long as the watcher's parent.
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, &slot]() {
        watcher->deleteLater();
        const QDBusPendingReply<uint> reply = *watcher;
        slot.pending = false;
        if (reply.isValid()) {
            slot.id = reply.value();
            slot.sentAt.start();
            ownIds_.insert(slot.id);
        }
        if (const std::optional<Request> queued = std::exchange(slot.queued, std::nullopt))
            send(slot, *queued);
    });
}

} // namespace Integration
