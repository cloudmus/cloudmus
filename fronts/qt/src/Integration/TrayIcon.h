#pragma once

#include <QObject>
#include <QString>

class QWidget;
class QSystemTrayIcon;
class QAction;
class QPixmap;

namespace Integration {

// QSystemTrayIcon + context menu (Prev/Play-Pause/Next/Stop/Show-Hide/Quit).
// Left-click toggles the main window's visibility. The tray's own tooltip
// is plain-text only on Linux (no image parameter in Qt's API) — cover art
// is delivered separately via NotificationToast, see that class.
class TrayIcon : public QObject {
    Q_OBJECT

public:
    TrayIcon(QWidget* mainWindow, QObject* parent = nullptr);

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

    QWidget* mainWindow_;
    QSystemTrayIcon* trayIcon_ = nullptr;
    QAction* playPauseAction_ = nullptr;
};

} // namespace Integration
