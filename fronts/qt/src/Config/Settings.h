#pragma once

#include <QByteArray>
#include <QList>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>

#include "PlayMode.h"

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
    explicit Settings(const QString& filePath = { });
    QString filePath() const { return settings_.fileName(); }

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);
    // The main window's unmaximized size, kept apart from windowGeometry():
    // the normal geometry in that blob can't be trusted on Wayland (see
    // Ui::MainWindow::normalSize_). Invalid when not saved yet.
    QSize windowNormalSize() const;
    void setWindowNormalSize(const QSize& size);

    QByteArray settingsDialogGeometry() const;
    void setSettingsDialogGeometry(const QByteArray& geometry);

    // The share of its splitter the sidebar / hero panel takes, in (0, 1) —
    // a fraction rather than pixels, so it comes back right at any window
    // size and the panes keep scaling with the window.
    double sidebarFraction() const;
    void setSidebarFraction(double fraction);

    double heroPanelFraction() const;
    void setHeroPanelFraction(double fraction);

    // The sidebar's collapsed rows, by ViewModel::SidebarModel::nodeKey() — every
    // other row starts expanded.
    QStringList sidebarCollapsed() const;
    void setSidebarCollapsed(const QStringList& keys);
    // What the sidebar last had selected (MainWindow::selectionKey()), to
    // open again at startup; empty for nothing.
    QString sidebarSelection() const;
    void setSidebarSelection(const QString& key);

    // Normal and quiet mode each keep their own level; `quiet` is which one
    // is in effect.
    int volume() const;
    void setVolume(int volume0To100);
    int quietVolume() const;
    void setQuietVolume(int volume0To100);
    bool quiet() const;
    void setQuiet(bool on);

    // The play modes as the user last set them (Playback::PlaybackController).
    bool shuffle() const;
    void setShuffle(bool on);
    Playback::RepeatMode repeatMode() const;
    void setRepeatMode(Playback::RepeatMode mode);

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
    QString lastActiveTrackId() const;
    int lastActiveTrackIndex() const;
    void setLastActiveTrack(const QString& trackId, int index);

    // Start playing the restored playlist at startup if it was playing
    // when the app last quit (ViewModel::ActivePlaylist). Off by default.
    bool resumePlaybackAtStartup() const;
    void setResumePlaybackAtStartup(bool on);
    // Whether playback was going, as the user left it — kept up to date
    // while it plays, so a logout or a kill counts too.
    bool wasPlaying() const;
    void setWasPlaying(bool playing);

    QString lastSourceId() const;
    void setLastSourceId(const QString& sourceId);

    // Glass (Theme::glassEnabled()) as the user chose it, or nullopt if
    // they never did — Theme::glassByDefault() decides then. It's still
    // only on where the window system can blur (Integration::WindowGlass).
    // Read once at startup.
    std::optional<bool> glassBackground() const;
    void setGlassBackground(bool on);

    // Light or dark look, or System: whatever the desktop's scheme is.
    enum class ColorScheme {
        System,
        Light,
        Dark
    };
    ColorScheme colorScheme() const;
    void setColorScheme(ColorScheme scheme);

    // A system notification on every track change. Off by default on
    // Windows, where each one also piles up in the Action Center.
    bool trackNotifications() const;
    void setTrackNotifications(bool on);

    // One hotkey as the user set it (Hotkeys::actions() names them by id).
    // `key` is a QKeySequence in portable text, empty for none.
    struct HotkeyConfig {
        QString key;
        // Works with the window out of focus (where the desktop allows it),
        // not just inside the app.
        bool global = true;
        // Says what the action did in a notification (for the actions that can).
        bool notify = true;

        bool operator==(const HotkeyConfig&) const = default;
    };
    // nullopt until the user changed it: the action's defaults apply.
    std::optional<HotkeyConfig> hotkey(const QString& id) const;
    void setHotkey(const QString& id, const HotkeyConfig& config);
    void resetHotkey(const QString& id);

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

    // The playlist ids the user put at a source's top level in the sidebar,
    // in order — nullopt until they first change it, when the source's own
    // Playlist.featured suggestion applies instead (ViewModel::SidebarModel).
    std::optional<QStringList> favorites(const QString& sourceId) const;
    void setFavorites(const QString& sourceId, const QStringList& playlistIds);
    // Whether the user was told, once, where a station taken out of the
    // favorites went (MainWindow::toggleFavorite()).
    bool hiddenFavoriteHintShown() const;
    void setHiddenFavoriteHintShown();

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
    // Whether saving tracks is on at all — off until the user turns it on
    // (and agrees to what downloads are for, see Ui::Settings::DownloadsPage).
    bool downloadsEnabled() const;
    void setDownloadsEnabled(bool on);

    // The update the user declined (Update::UpdateChecker): not offered
    // again at startup — a newer one is.
    QString skippedUpdateVersion() const;
    void setSkippedUpdateVersion(const QString& version);

    bool analyticsEnabled() const;
    void setAnalyticsEnabled(bool on);
    // Random installation identifier, kept when analytics is switched off.
    QString analyticsClientId();
    // Numbers this run's analytics session: 1 for the installation's first.
    int nextAnalyticsSession();

private:
    void restrictPermissions();

    QSettings settings_;
};

} // namespace Config
