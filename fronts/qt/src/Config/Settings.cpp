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

// Default 430px favors the hero panel over the track list (~60/40 of the
// ~720px content area left after the default 960px window width and 240px
// sidebar), per the UI design.
int Settings::heroPanelWidth() const { return settings_.value(QStringLiteral("window/heroPanelWidth"), 430).toInt(); }

void Settings::setHeroPanelWidth(int width) { settings_.setValue(QStringLiteral("window/heroPanelWidth"), width); }

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
