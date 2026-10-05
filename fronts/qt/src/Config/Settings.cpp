#include "Settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>

#include <limits>
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

Settings::Settings(const QString& filePath)
    : settings_(filePath.isEmpty() ? configFilePath() : filePath, QSettings::IniFormat)
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

QSize Settings::windowNormalSize() const { return settings_.value(QStringLiteral("window/normalSize")).toSize(); }

void Settings::setWindowNormalSize(const QSize& size) { settings_.setValue(QStringLiteral("window/normalSize"), size); }

QByteArray Settings::settingsDialogGeometry() const
{
    return settings_.value(QStringLiteral("settingsDialog/geometry")).toByteArray();
}

void Settings::setSettingsDialogGeometry(const QByteArray& geometry)
{
    settings_.setValue(QStringLiteral("settingsDialog/geometry"), geometry);
}

namespace {
// A hand-edited or broken value can't shut a pane or push it past the other.
double splitFraction(const QSettings& settings, const QString& key, double fallback)
{
    bool ok = false;
    const double value = settings.value(key, fallback).toDouble(&ok);
    return ok && value > 0.0 && value < 1.0 ? value : fallback;
}
} // namespace

// Default a quarter: 240px of the default 960px window.
double Settings::sidebarFraction() const
{
    return splitFraction(settings_, QStringLiteral("window/sidebarFraction"), 0.25);
}

void Settings::setSidebarFraction(double fraction)
{
    settings_.setValue(QStringLiteral("window/sidebarFraction"), fraction);
}

// Default 60/40 in favor of the hero panel over the track list, per the UI
// design.
double Settings::heroPanelFraction() const
{
    return splitFraction(settings_, QStringLiteral("window/heroPanelFraction"), 0.6);
}

void Settings::setHeroPanelFraction(double fraction)
{
    settings_.setValue(QStringLiteral("window/heroPanelFraction"), fraction);
}

Settings::ActivePlaylistRef Settings::lastActivePlaylist() const
{
    return { settings_.value(QStringLiteral("playback/activeSourceId")).toString(),
        settings_.value(QStringLiteral("playback/activePlaylistId")).toString(),
        settings_.value(QStringLiteral("playback/activePlaylistKind")).toString() };
}

void Settings::setLastActivePlaylist(const ActivePlaylistRef& ref)
{
    const ActivePlaylistRef previous = lastActivePlaylist();
    if (previous.sourceId != ref.sourceId || previous.playlistId != ref.playlistId || previous.kind != ref.kind) {
        settings_.remove(QStringLiteral("playback/activeTrackId"));
        settings_.remove(QStringLiteral("playback/activeTrackIndex"));
    }
    settings_.setValue(QStringLiteral("playback/activeSourceId"), ref.sourceId);
    settings_.setValue(QStringLiteral("playback/activePlaylistId"), ref.playlistId);
    settings_.setValue(QStringLiteral("playback/activePlaylistKind"), ref.kind);
}

QString Settings::lastActiveTrackId() const
{
    return settings_.value(QStringLiteral("playback/activeTrackId")).toString();
}

int Settings::lastActiveTrackIndex() const
{
    return settings_.value(QStringLiteral("playback/activeTrackIndex"), -1).toInt();
}

void Settings::setLastActiveTrack(const QString& trackId, int index)
{
    settings_.setValue(QStringLiteral("playback/activeTrackId"), trackId);
    settings_.setValue(QStringLiteral("playback/activeTrackIndex"), index);
}

bool Settings::resumePlaybackAtStartup() const
{
    return settings_.value(QStringLiteral("playback/resumeAtStartup"), false).toBool();
}

void Settings::setResumePlaybackAtStartup(bool on)
{
    settings_.setValue(QStringLiteral("playback/resumeAtStartup"), on);
}

bool Settings::wasPlaying() const { return settings_.value(QStringLiteral("playback/wasPlaying"), false).toBool(); }

void Settings::setWasPlaying(bool playing)
{
    if (playing != wasPlaying())
        settings_.setValue(QStringLiteral("playback/wasPlaying"), playing);
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

int Settings::quietVolume() const { return settings_.value(QStringLiteral("playback/quietVolume"), 30).toInt(); }

void Settings::setQuietVolume(int volume0To100)
{
    settings_.setValue(QStringLiteral("playback/quietVolume"), volume0To100);
}

bool Settings::quiet() const { return settings_.value(QStringLiteral("playback/quiet")).toBool(); }

void Settings::setQuiet(bool on) { settings_.setValue(QStringLiteral("playback/quiet"), on); }

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

QString Settings::language() const { return settings_.value(QStringLiteral("ui/language")).toString(); }

void Settings::setLanguage(const QString& code)
{
    const QString key = QStringLiteral("ui/language");
    if (code.isEmpty())
        settings_.remove(key);
    else
        settings_.setValue(key, code);
}

Settings::ColorScheme Settings::colorScheme() const
{
    const QString value = settings_.value(QStringLiteral("window/colorScheme")).toString();
    if (value == QLatin1String("light"))
        return ColorScheme::Light;
    if (value == QLatin1String("dark"))
        return ColorScheme::Dark;
    return ColorScheme::System;
}

void Settings::setColorScheme(ColorScheme scheme)
{
    const QString key = QStringLiteral("window/colorScheme");
    if (scheme == ColorScheme::System)
        settings_.remove(key);
    else
        settings_.setValue(key, scheme == ColorScheme::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
}

bool Settings::trackNotifications() const
{
#ifdef Q_OS_WIN
    constexpr bool byDefault = false;
#else
    constexpr bool byDefault = true;
#endif
    return settings_.value(QStringLiteral("notifications/trackChange"), byDefault).toBool();
}

void Settings::setTrackNotifications(bool on) { settings_.setValue(QStringLiteral("notifications/trackChange"), on); }

std::optional<Settings::HotkeyConfig> Settings::hotkey(const QString& id) const
{
    const QString group = QStringLiteral("hotkeys/") + id + QLatin1Char('/');
    if (!settings_.contains(group + QStringLiteral("key")))
        return std::nullopt;
    HotkeyConfig config;
    config.key = settings_.value(group + QStringLiteral("key")).toString();
    config.global = settings_.value(group + QStringLiteral("global"), true).toBool();
    config.notify = settings_.value(group + QStringLiteral("notify"), true).toBool();
    config.sound = settings_.value(group + QStringLiteral("sound"), true).toBool();
    return config;
}

void Settings::setHotkey(const QString& id, const HotkeyConfig& config)
{
    const QString group = QStringLiteral("hotkeys/") + id + QLatin1Char('/');
    // An empty key is stored too (it means "none", not "default"), which is
    // why hotkey() looks for the key's presence.
    settings_.setValue(group + QStringLiteral("key"), config.key);
    settings_.setValue(group + QStringLiteral("global"), config.global);
    settings_.setValue(group + QStringLiteral("notify"), config.notify);
    settings_.setValue(group + QStringLiteral("sound"), config.sound);
}

void Settings::resetHotkey(const QString& id) { settings_.remove(QStringLiteral("hotkeys/") + id); }

QString Settings::skippedUpdateVersion() const
{
    return settings_.value(QStringLiteral("updates/skippedVersion")).toString();
}

void Settings::setSkippedUpdateVersion(const QString& version)
{
    settings_.setValue(QStringLiteral("updates/skippedVersion"), version);
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

bool Settings::downloadsEnabled() const { return settings_.value(QStringLiteral("download/enabled"), false).toBool(); }

void Settings::setDownloadsEnabled(bool on) { settings_.setValue(QStringLiteral("download/enabled"), on); }

bool Settings::analyticsEnabled() const { return settings_.value(QStringLiteral("analytics/enabled"), true).toBool(); }

void Settings::setAnalyticsEnabled(bool on)
{
    settings_.setValue(QStringLiteral("analytics/enabled"), on);
    settings_.sync();
    restrictPermissions();
}

QString Settings::analyticsClientId()
{
    const QString key = QStringLiteral("analytics/clientId");
    QString id = settings_.value(key).toString();
    // The web tag's own shape, "<random>.<first seen, Unix seconds>"; an
    // older random UUID is replaced.
    static const QRegularExpression shape(QStringLiteral("^\\d+\\.\\d+$"));
    if (!shape.match(id).hasMatch()) {
        id = QStringLiteral("%1.%2")
                 .arg(QRandomGenerator::global()->bounded(1, std::numeric_limits<int>::max()))
                 .arg(QDateTime::currentSecsSinceEpoch());
        settings_.setValue(key, id);
        settings_.sync();
        restrictPermissions();
    }
    return id;
}

int Settings::nextAnalyticsSession()
{
    const QString key = QStringLiteral("analytics/sessionCount");
    const int session = settings_.value(key, 0).toInt() + 1;
    settings_.setValue(key, session);
    settings_.sync();
    restrictPermissions();
    return session;
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
