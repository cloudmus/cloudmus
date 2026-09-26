#include "Settings.h"

#include <QDir>
#include <QFile>
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
    restrictPermissions();
}

void Settings::restrictPermissions()
{
    // Owner-only: the file holds proxy passwords. Re-applied on every
    // start and after writing them, in case the file was just created or
    // rewritten with the umask's permissions.
    if (QFile::exists(settings_.fileName()))
        QFile::setPermissions(settings_.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
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

QStringList Settings::sidebarCollapsed() const
{
    return settings_.value(QStringLiteral("sidebar/collapsed")).toStringList();
}

void Settings::setSidebarCollapsed(const QStringList& keys)
{
    settings_.setValue(QStringLiteral("sidebar/collapsed"), keys);
}

QString Settings::sidebarSelection() const { return settings_.value(QStringLiteral("sidebar/selection")).toString(); }

void Settings::setSidebarSelection(const QString& key) { settings_.setValue(QStringLiteral("sidebar/selection"), key); }

int Settings::volume() const { return settings_.value(QStringLiteral("playback/volume"), 100).toInt(); }

void Settings::setVolume(int volume0To100) { settings_.setValue(QStringLiteral("playback/volume"), volume0To100); }

bool Settings::shuffle() const { return settings_.value(QStringLiteral("playback/shuffle")).toBool(); }

void Settings::setShuffle(bool on) { settings_.setValue(QStringLiteral("playback/shuffle"), on); }

Playback::RepeatMode Settings::repeatMode() const
{
    const QString value = settings_.value(QStringLiteral("playback/repeat")).toString();
    if (value == QStringLiteral("all"))
        return Playback::RepeatMode::All;
    if (value == QStringLiteral("one"))
        return Playback::RepeatMode::One;
    return Playback::RepeatMode::Off;
}

void Settings::setRepeatMode(Playback::RepeatMode mode)
{
    const char* value = mode == Playback::RepeatMode::All ? "all" : mode == Playback::RepeatMode::One ? "one" : "off";
    settings_.setValue(QStringLiteral("playback/repeat"), QString::fromLatin1(value));
}

QString Settings::lastSourceId() const { return settings_.value(QStringLiteral("playback/lastSourceId")).toString(); }

void Settings::setLastSourceId(const QString& sourceId)
{
    settings_.setValue(QStringLiteral("playback/lastSourceId"), sourceId);
}

std::optional<bool> Settings::glassBackground() const
{
    const QString key = QStringLiteral("window/glass");
    if (!settings_.contains(key))
        return std::nullopt;
    return settings_.value(key).toBool();
}

void Settings::setGlassBackground(bool on) { settings_.setValue(QStringLiteral("window/glass"), on); }

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

std::optional<QStringList> Settings::favorites(const QString& sourceId) const
{
    const QString key = QStringLiteral("favorites/") + sourceId;
    if (!settings_.contains(key))
        return std::nullopt;
    return settings_.value(key).toStringList();
}

void Settings::setFavorites(const QString& sourceId, const QStringList& playlistIds)
{
    settings_.setValue(QStringLiteral("favorites/") + sourceId, playlistIds);
}

bool Settings::hiddenFavoriteHintShown() const
{
    return settings_.value(QStringLiteral("hints/hiddenFavorite")).toBool();
}

void Settings::setHiddenFavoriteHintShown() { settings_.setValue(QStringLiteral("hints/hiddenFavorite"), true); }

QList<ProxyConfig> Settings::proxies() const
{
    QList<ProxyConfig> result;
    auto& settings = const_cast<QSettings&>(settings_); // beginReadArray() isn't const
    const int count = settings.beginReadArray(QStringLiteral("proxies"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        ProxyConfig proxy;
        proxy.id = settings.value(QStringLiteral("id")).toString();
        proxy.name = settings.value(QStringLiteral("name")).toString();
        proxy.type = settings.value(QStringLiteral("type")).toString() == QLatin1String("socks5")
            ? ProxyConfig::Type::Socks5
            : ProxyConfig::Type::Http;
        proxy.host = settings.value(QStringLiteral("host")).toString();
        proxy.port = settings.value(QStringLiteral("port")).toInt();
        proxy.username = settings.value(QStringLiteral("username")).toString();
        proxy.password = settings.value(QStringLiteral("password")).toString();
        if (!proxy.id.isEmpty())
            result.append(proxy);
    }
    settings.endArray();
    return result;
}

void Settings::setProxies(const QList<ProxyConfig>& proxies)
{
    settings_.remove(QStringLiteral("proxies"));
    settings_.beginWriteArray(QStringLiteral("proxies"), int(proxies.size()));
    for (int i = 0; i < proxies.size(); ++i) {
        const ProxyConfig& proxy = proxies[i];
        settings_.setArrayIndex(i);
        settings_.setValue(QStringLiteral("id"), proxy.id);
        settings_.setValue(QStringLiteral("name"), proxy.name);
        settings_.setValue(QStringLiteral("type"),
            proxy.type == ProxyConfig::Type::Socks5 ? QStringLiteral("socks5") : QStringLiteral("http"));
        settings_.setValue(QStringLiteral("host"), proxy.host);
        settings_.setValue(QStringLiteral("port"), proxy.port);
        settings_.setValue(QStringLiteral("username"), proxy.username);
        settings_.setValue(QStringLiteral("password"), proxy.password);
    }
    settings_.endArray();
    settings_.sync();
    restrictPermissions();
}

QString Settings::sourceConnection(const QString& sourceId) const
{
    return settings_.value(QStringLiteral("sources/%1/connection").arg(sourceId), QLatin1String(kSystemConnection))
        .toString();
}

void Settings::setSourceConnection(const QString& sourceId, const QString& connection)
{
    const QString key = QStringLiteral("sources/%1/connection").arg(sourceId);
    if (connection == QLatin1String(kSystemConnection))
        settings_.remove(key);
    else
        settings_.setValue(key, connection);
}

} // namespace Config
