#pragma once

#include <QObject>
#include <QString>

class QSystemTrayIcon;
class QAction;
class QMenu;

namespace Ui {
class WindowHost;
}
namespace ViewModel {
class NowPlaying;
}
namespace Hotkeys {
class Registry;
}
namespace App {
class PlaylistEditing;
}

namespace Integration {

// QSystemTrayIcon + context menu (Prev/Play-Pause/Next/Stop, the playing
// track's Like/Dislike/Playlists, Show-Hide, Quit), all following
// ViewModel::NowPlaying. Left-click toggles the main window's visibility —
// see Ui::WindowHost::toggleShown(). The tray's own tooltip
// is plain-text only on Linux (no image parameter in Qt's API) — cover art
// is delivered separately via NotificationToast, see that class.
class TrayIcon : public QObject {
    Q_OBJECT

public:
    TrayIcon(Ui::WindowHost& windowHost, ViewModel::NowPlaying& nowPlaying, App::PlaylistEditing& playlistEditing,
        Hotkeys::Registry& hotkeys, QObject* parent = nullptr);
    QSystemTrayIcon* systemTrayIcon() const { return trayIcon_; }

signals:
    void quitRequested();

private:
    // Picks tray_icon_dark.svg/tray_icon_light.svg (see icons.qrc) to match
    // the panel's color — a plain white glyph reads fine on a dark panel but
    // disappears on a light one, and vice versa. The panel's color is the
    // taskbar setting on Windows, always dark on GNOME, and the color scheme
    // elsewhere. Called once at construction and again whenever it changes live.
    void updateTrayIcon();
    // Like/Dislike/Playlists: shown for what the playing track's source
    // supports, labeled and iconed by its current state.
    void refreshFeedbackActions();
    // "Hide" while the window is on screen, "Show" otherwise.
    void refreshShowHideAction();
    // The tooltip names the playing track; the play/pause action says what
    // a click would do.
    void refreshTrack();
    void refreshPlaying();
    // The playing track's playlists as checkable actions — a tray menu is
    // exported over D-Bus, where widget rows don't exist.
    void fillPlaylistsMenu();
    // Each item shows the key of its action, as the settings have it.
    void refreshHotkeys();

    Ui::WindowHost& windowHost_;
    ViewModel::NowPlaying& nowPlaying_;
    App::PlaylistEditing& playlistEditing_;
    Hotkeys::Registry& hotkeys_;
    QSystemTrayIcon* trayIcon_ = nullptr;
    QAction* previousAction_ = nullptr;
    QAction* playPauseAction_ = nullptr;
    QAction* nextAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* feedbackSeparator_ = nullptr;
    QAction* likeAction_ = nullptr;
    QAction* dislikeAction_ = nullptr;
    QMenu* playlistsMenu_ = nullptr;
    QAction* showHideAction_ = nullptr;
};

} // namespace Integration
