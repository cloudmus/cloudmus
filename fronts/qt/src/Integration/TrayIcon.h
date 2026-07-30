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
    QWidget* mainWindow_;
    QSystemTrayIcon* trayIcon_ = nullptr;
    QAction* playPauseAction_ = nullptr;
};

} // namespace Integration
