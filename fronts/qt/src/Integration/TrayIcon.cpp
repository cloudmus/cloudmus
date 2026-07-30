#include "TrayIcon.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWidget>

namespace Integration {

TrayIcon::TrayIcon(QWidget* mainWindow, QObject* parent)
    : QObject(parent)
    , mainWindow_(mainWindow)
{
    trayIcon_ = new QSystemTrayIcon(QIcon::fromTheme(QStringLiteral("cloudmus"), qApp->windowIcon()), this);
    trayIcon_->setToolTip(QStringLiteral("cloudmus"));

    auto* menu = new QMenu();
    auto* previousAction = menu->addAction(QIcon::fromTheme(QStringLiteral("media-skip-backward")), tr("Previous"));
    playPauseAction_ = menu->addAction(QIcon::fromTheme(QStringLiteral("media-playback-start")), tr("Play"));
    auto* nextAction = menu->addAction(QIcon::fromTheme(QStringLiteral("media-skip-forward")), tr("Next"));
    auto* stopAction = menu->addAction(QIcon::fromTheme(QStringLiteral("media-playback-stop")), tr("Stop"));
    menu->addSeparator();
    auto* showHideAction = menu->addAction(tr("Show/Hide"));
    menu->addSeparator();
    auto* quitAction = menu->addAction(tr("Quit"));

    connect(previousAction, &QAction::triggered, this, &TrayIcon::previousRequested);
    connect(playPauseAction_, &QAction::triggered, this, &TrayIcon::playPauseRequested);
    connect(nextAction, &QAction::triggered, this, &TrayIcon::nextRequested);
    connect(stopAction, &QAction::triggered, this, &TrayIcon::stopRequested);
    connect(showHideAction, &QAction::triggered, this,
            [this]() { mainWindow_->setVisible(!mainWindow_->isVisible()); });
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    trayIcon_->setContextMenu(menu);

    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            mainWindow_->setVisible(!mainWindow_->isVisible());
            if (mainWindow_->isVisible()) {
                mainWindow_->raise();
                mainWindow_->activateWindow();
            }
        }
    });

    trayIcon_->show();
}

void TrayIcon::setNowPlayingTooltip(const QString& title, const QString& artist)
{
    trayIcon_->setToolTip(artist.isEmpty() ? title : QStringLiteral("%1 — %2").arg(title, artist));
}

void TrayIcon::setPlaying(bool playing)
{
    playPauseAction_->setText(playing ? tr("Pause") : tr("Play"));
    playPauseAction_->setIcon(
        QIcon::fromTheme(playing ? QStringLiteral("media-playback-pause") : QStringLiteral("media-playback-start")));
}

} // namespace Integration
