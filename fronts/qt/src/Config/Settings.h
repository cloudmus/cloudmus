#pragma once

#include <QByteArray>
#include <QList>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace Config {

// A named proxy from the user's list; sources pick one by `id` (see
// Settings::sourceConnection()).
struct ProxyConfig {
    enum class Type {
        Http,
        Socks5
    };

    // Stable across renames, so a source's choice survives them.
    QString id;
    QString name;
    Type type = Type::Http;
    QString host;
    int port = 0;
    QString username;
    QString password;

    bool operator==(const ProxyConfig&) const = default;
};

// QSettings-backed, ~/.config/cloudmus/fronts/qt/config.ini
class Settings {
public:
    Settings();

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);

    QByteArray settingsDialogGeometry() const;
    void setSettingsDialogGeometry(const QByteArray& geometry);

    int sidebarWidth() const;
    void setSidebarWidth(int width);

    int heroPanelWidth() const;
    void setHeroPanelWidth(int width);

    int volume() const;
    void setVolume(int volume0To100);

    // The playlist the main area last showed as active (see MainWindow's
    // ActiveContext) — restored on startup without starting playback.
    // `kind` is the Playlist.kind, or "history" for the History view.
    struct ActivePlaylistRef {
        QString sourceId;
        QString playlistId;
        QString kind;
    };
    ActivePlaylistRef lastActivePlaylist() const;
    void setLastActivePlaylist(const ActivePlaylistRef& ref);

    QString lastSourceId() const;
    void setLastSourceId(const QString& sourceId);

    bool closeMinimizesToTray() const;
    void setCloseMinimizesToTray(bool value);

    // Launched at login (Integration::Autostart): stay in the tray instead
    // of opening the window.
    bool startHiddenAtLogin() const;
    void setStartHiddenAtLogin(bool value);

    // The user's proxies, in their order. Stored with passwords in the
    // clear, which is why the config file is kept private to the user
    // (see the constructor).
    QList<ProxyConfig> proxies() const;
    void setProxies(const QList<ProxyConfig>& proxies);

    // How a source connects: kSystemConnection (inherit the environment's
    // proxy settings, if any — the default), kDirectConnection, or a
    // ProxyConfig::id.
    static constexpr auto kSystemConnection = "system";
    static constexpr auto kDirectConnection = "direct";
    QString sourceConnection(const QString& sourceId) const;
    void setSourceConnection(const QString& sourceId, const QString& connection);

    // Backend manifest ids the user switched off (Rpc::SourceManager).
    QStringList disabledSources() const;
    void setDisabledSources(const QStringList& ids);

    // Subfolders of the download folder a track is saved into — see
    // Library::downloadDirectoryFor().
    enum class DownloadLayout {
        Flat,
        BySource, // <source name>/
        ByArtist, // <artist>/
        ByArtistAlbum, // <artist>/<album>/
    };
    DownloadLayout downloadLayout() const;
    void setDownloadLayout(DownloadLayout layout);

    static QString defaultDownloadDirectory();
    QString downloadDirectory() const;
    // An empty path or the default one resets to the default.
    void setDownloadDirectory(const QString& path);

private:
    void restrictPermissions();

    QSettings settings_;
};

} // namespace Config
