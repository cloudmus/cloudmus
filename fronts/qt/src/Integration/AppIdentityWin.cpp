#include "AppIdentity.h"

#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QPixmap>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace Integration::AppIdentity {

namespace {
// What an unpackaged app registers instead of the Start Menu shortcut with
// System.AppUserModel.ID that the shell otherwise looks for — the way the
// Windows App SDK registers one for its toasts.
constexpr auto kRegistryKey = "HKEY_CURRENT_USER\\Software\\Classes\\AppUserModelId\\";

// The shell takes the icon from a file only; written again only when the
// icon changed, not at every start.
QString writeIconFile(const QIcon& icon)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!icon.pixmap(QSize(256, 256), 1.0).save(&buffer, "PNG"))
        return { };
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString path = dir + QStringLiteral("/app-icon.png");
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == png)
        return path;
    existing.close();
    QDir().mkpath(dir);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size() || !file.commit()) {
        qWarning() << "AppIdentity: can't write" << path << file.errorString();
        return { };
    }
    return path;
}
} // namespace

void registerAppUserModelId(const QIcon& icon)
{
    QSettings settings(
        QString::fromLatin1(kRegistryKey) + QString::fromWCharArray(kAppUserModelId), QSettings::NativeFormat);
    settings.setValue(QStringLiteral("DisplayName"), QStringLiteral("CloudMus"));
    const QString iconPath = writeIconFile(icon);
    if (!iconPath.isEmpty())
        settings.setValue(QStringLiteral("IconUri"), QDir::toNativeSeparators(iconPath));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        qWarning() << "AppIdentity: registry update failed" << settings.status();
}

} // namespace Integration::AppIdentity
