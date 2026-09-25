#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace Config {

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
    QSettings settings_;
};

} // namespace Config
