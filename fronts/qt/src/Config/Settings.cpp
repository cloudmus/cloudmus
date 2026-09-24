#include "Settings.h"

#include <QDir>
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

Settings::ActivePlaylistRef Settings::lastActivePlaylist() const
{
    return { settings_.value(QStringLiteral("playback/activeSourceId")).toString(),
        settings_.value(QStringLiteral("playback/activePlaylistId")).toString(),
        settings_.value(QStringLiteral("playback/activePlaylistKind")).toString() };
}

void Settings::setLastActivePlaylist(const ActivePlaylistRef& ref)
{
    settings_.setValue(QStringLiteral("playback/activeSourceId"), ref.sourceId);
    settings_.setValue(QStringLiteral("playback/activePlaylistId"), ref.playlistId);
    settings_.setValue(QStringLiteral("playback/activePlaylistKind"), ref.kind);
}

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

QString Settings::defaultDownloadDirectory()
{
    // The XDG music folder (`xdg-user-dir MUSIC`, e.g. ~/Музыка) — also the
    // local-folder backend's default, so downloads show up there.
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
}

QString Settings::downloadDirectory() const
{
    return settings_.value(QStringLiteral("download/directory"), defaultDownloadDirectory()).toString();
}

void Settings::setDownloadDirectory(const QString& path)
{
    // Only a folder the user actually chose is stored: saving the default
    // too would pin it, and it would stop following the XDG music folder.
    const QString cleaned = QDir::cleanPath(path.trimmed());
    if (path.trimmed().isEmpty() || cleaned == QDir::cleanPath(defaultDownloadDirectory()))
        settings_.remove(QStringLiteral("download/directory"));
    else
        settings_.setValue(QStringLiteral("download/directory"), cleaned);
}

} // namespace Config
