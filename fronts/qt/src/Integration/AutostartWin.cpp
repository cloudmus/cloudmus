#include "Autostart.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>

namespace Integration::Autostart {

namespace {
constexpr auto kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kValueName = "CloudMus";

QString command()
{
    return u'"' + QCoreApplication::applicationFilePath().replace(u'/', u'\\') + u'"' + u' '
        + QString::fromLatin1(kLaunchedAtLoginArgument);
}
} // namespace

bool isEnabled()
{
    QSettings settings(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return settings.contains(QString::fromLatin1(kValueName));
}

bool setEnabled(bool enabled)
{
    QSettings settings(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (enabled)
        settings.setValue(QString::fromLatin1(kValueName), command());
    else
        settings.remove(QString::fromLatin1(kValueName));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        qWarning() << "Autostart: registry update failed" << settings.status();
    return settings.status() == QSettings::NoError;
}

void refresh()
{
    if (!isEnabled())
        return;
    QSettings settings(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (settings.value(QString::fromLatin1(kValueName)).toString() != command())
        setEnabled(true);
}

} // namespace Integration::Autostart
