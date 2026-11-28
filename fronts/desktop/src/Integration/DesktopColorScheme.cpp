#include "DesktopColorScheme.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QStyleHints>

namespace Integration {

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.Settings");
const QString kNamespace = QStringLiteral("org.freedesktop.appearance");
const QString kKey = QStringLiteral("color-scheme");

} // namespace

DesktopColorScheme::DesktopColorScheme(QObject* parent)
    : QObject(parent)
{
    QDBusConnection::sessionBus().connect(kService, kPath, kInterface, QStringLiteral("SettingChanged"), this,
                                          SLOT(onSettingChanged(QString, QString, QDBusVariant)));
    read(QStringLiteral("ReadOne"));
}

DesktopColorScheme& DesktopColorScheme::instance()
{
    static DesktopColorScheme instance;
    return instance;
}

Qt::ColorScheme DesktopColorScheme::resolved()
{
    const Qt::ColorScheme qt = QGuiApplication::styleHints()->colorScheme();
    return qt != Qt::ColorScheme::Unknown ? qt : instance().scheme();
}

void DesktopColorScheme::read(const QString& method)
{
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    call << kNamespace << kKey;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, method](QDBusPendingCallWatcher* done) {
        done->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *done;
        if (reply.isError()) {
            // Portals before version 2 only have Read.
            if (method == QLatin1String("ReadOne"))
                read(QStringLiteral("Read"));
            return;
        }
        QVariant value = reply.value().variant();
        // Read wraps the value in one more variant.
        if (value.canConvert<QDBusVariant>())
            value = value.value<QDBusVariant>().variant();
        set(value);
    });
}

void DesktopColorScheme::onSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value)
{
    if (ns == kNamespace && key == kKey)
        set(value.variant());
}

void DesktopColorScheme::set(const QVariant& value)
{
    // 0: no preference, 1: prefer dark, 2: prefer light.
    const uint code = value.toUInt();
    const Qt::ColorScheme scheme = code == 1 ? Qt::ColorScheme::Dark
        : code == 2                          ? Qt::ColorScheme::Light
                                             : Qt::ColorScheme::Unknown;
    if (scheme == scheme_)
        return;
    scheme_ = scheme;
    emit changed();
}

} // namespace Integration
