#include "Settings.h"

#include <QStandardPaths>

namespace Config {

namespace {
QString configFilePath()
{
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return configHome + QStringLiteral("/cloudmus/fronts/qt/config.ini");
}
} // namespace

Settings::Settings()
    : settings_(configFilePath(), QSettings::IniFormat)
{
}

QByteArray Settings::windowGeometry() const { return settings_.value(QStringLiteral("window/geometry")).toByteArray(); }

void Settings::setWindowGeometry(const QByteArray& geometry)
{
    settings_.setValue(QStringLiteral("window/geometry"), geometry);
}

int Settings::sidebarWidth() const { return settings_.value(QStringLiteral("window/sidebarWidth"), 240).toInt(); }

void Settings::setSidebarWidth(int width) { settings_.setValue(QStringLiteral("window/sidebarWidth"), width); }

int Settings::volume() const { return settings_.value(QStringLiteral("playback/volume"), 100).toInt(); }

void Settings::setVolume(int volume0To100) { settings_.setValue(QStringLiteral("playback/volume"), volume0To100); }

QString Settings::lastSourceId() const { return settings_.value(QStringLiteral("playback/lastSourceId")).toString(); }

void Settings::setLastSourceId(const QString& sourceId)
{
    settings_.setValue(QStringLiteral("playback/lastSourceId"), sourceId);
}

bool Settings::closeMinimizesToTray() const
{
    return settings_.value(QStringLiteral("window/closeMinimizesToTray"), true).toBool();
}

void Settings::setCloseMinimizesToTray(bool value)
{
    settings_.setValue(QStringLiteral("window/closeMinimizesToTray"), value);
}

QString Settings::downloadDirectory() const
{
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return settings_.value(QStringLiteral("download/directory"), fallback).toString();
}

void Settings::setDownloadDirectory(const QString& path)
{
    settings_.setValue(QStringLiteral("download/directory"), path);
}

} // namespace Config
