#pragma once

#include <QObject>
#include <QString>

class QSystemTrayIcon;
class QAction;
class QMenu;

namespace Ui {
class MainWindow;
}

namespace Integration {

// QSystemTrayIcon + context menu (Prev/Play-Pause/Next/Stop, the playing
// track's Like/Dislike/Playlists, Show-Hide, Quit). Left-click toggles the
// main window's visibility — see MainWindow::toggleShown(). The tray's own tooltip
// is plain-text only on Linux (no image parameter in Qt's API) — cover art
// is delivered separately via NotificationToast, see that class.
class TrayIcon : public QObject {
    Q_OBJECT

public:
    TrayIcon(Ui::MainWindow* mainWindow, QObject* parent = nullptr);

    void setNowPlayingTooltip(const QString& title, const QString& artist);
    void setPlaying(bool playing);

signals:
    void previousRequested();
    void playPauseRequested();
    void nextRequested();
    void stopRequested();
    void quitRequested();

private:
    // Picks tray_icon_dark.svg/tray_icon_light.svg (see icons.qrc) to match
    // the current color scheme — a plain white glyph reads fine on a dark
    // panel but disappears on a light one, and vice versa. Called once at
    // construction and again whenever the desktop's scheme changes live.
    void updateTrayIcon();
    // Like/Dislike/Playlists: shown for what the playing track's source
    // supports, labeled and iconed by its current state.
    void refreshFeedbackActions();
    // "Hide" while the window is on screen, "Show" otherwise.
    void refreshShowHideAction();

    Ui::MainWindow* mainWindow_;
    QSystemTrayIcon* trayIcon_ = nullptr;
    QAction* playPauseAction_ = nullptr;
    QAction* feedbackSeparator_ = nullptr;
    QAction* likeAction_ = nullptr;
    QAction* dislikeAction_ = nullptr;
    QMenu* playlistsMenu_ = nullptr;
    QAction* showHideAction_ = nullptr;
};

} // namespace Integration
