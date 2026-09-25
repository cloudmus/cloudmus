#include "Settings.h"

#include <QDir>
#include <QStandardPaths>

#include <utility>

namespace Config {

namespace {
// Stored by name, not by enum value, so reordering the enum can't
// reinterpret an existing config.
constexpr std::pair<Settings::DownloadLayout, const char*> kDownloadLayoutNames[] = {
    { Settings::DownloadLayout::Flat, "flat" },
    { Settings::DownloadLayout::BySource, "source" },
    { Settings::DownloadLayout::ByArtist, "artist" },
    { Settings::DownloadLayout::ByArtistAlbum, "artistAlbum" },
};

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

QByteArray Settings::settingsDialogGeometry() const
{
    return settings_.value(QStringLiteral("settingsDialog/geometry")).toByteArray();
}

void Settings::setSettingsDialogGeometry(const QByteArray& geometry)
{
    settings_.setValue(QStringLiteral("settingsDialog/geometry"), geometry);
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

bool Settings::startHiddenAtLogin() const
{
    return settings_.value(QStringLiteral("window/startHiddenAtLogin"), true).toBool();
}

void Settings::setStartHiddenAtLogin(bool value)
{
    settings_.setValue(QStringLiteral("window/startHiddenAtLogin"), value);
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

Settings::DownloadLayout Settings::downloadLayout() const
{
    const QString name = settings_.value(QStringLiteral("download/layout")).toString();
    for (const auto& [layout, layoutName] : kDownloadLayoutNames) {
        if (name == QLatin1String(layoutName))
            return layout;
    }
    return DownloadLayout::Flat;
}

void Settings::setDownloadLayout(DownloadLayout layout)
{
    for (const auto& [candidate, name] : kDownloadLayoutNames) {
        if (candidate == layout)
            settings_.setValue(QStringLiteral("download/layout"), QString::fromLatin1(name));
    }
}

QStringList Settings::disabledSources() const
{
    return settings_.value(QStringLiteral("sources/disabled")).toStringList();
}

void Settings::setDisabledSources(const QStringList& ids)
{
    if (ids.isEmpty())
        settings_.remove(QStringLiteral("sources/disabled"));
    else
        settings_.setValue(QStringLiteral("sources/disabled"), ids);
}

} // namespace Config
