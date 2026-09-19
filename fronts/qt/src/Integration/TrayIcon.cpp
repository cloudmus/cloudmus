#include "TrayIcon.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QWidget>

#include "Icons.h"

namespace Integration {

TrayIcon::TrayIcon(QWidget* mainWindow, QObject* parent)
    : QObject(parent)
    , mainWindow_(mainWindow)
{
    trayIcon_ = new QSystemTrayIcon(this);
    updateTrayIcon();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &TrayIcon::updateTrayIcon);
    trayIcon_->setToolTip(QStringLiteral("CloudMus"));

    // Context-menu action icons are in scope for the Material Icons
    // migration (unlike the tray glyph itself, updateTrayIcon()'s brand
    // asset, untouched below) — a QMenu popup gets the design system's
    // Panel treatment via Theme::StyleSheet's global QMenu rule.
    auto* menu = new QMenu();
    auto* previousAction
        = menu->addAction(Theme::icon(QStringLiteral("skip_previous"), Theme::IconColor::Ink, 16), tr("Previous"));
    playPauseAction_
        = menu->addAction(Theme::icon(QStringLiteral("play_arrow"), Theme::IconColor::Ink, 16), tr("Play"));
    auto* nextAction = menu->addAction(Theme::icon(QStringLiteral("skip_next"), Theme::IconColor::Ink, 16), tr("Next"));
    auto* stopAction = menu->addAction(Theme::icon(QStringLiteral("stop"), Theme::IconColor::Ink, 16), tr("Stop"));
    menu->addSeparator();
    auto* showHideAction = menu->addAction(tr("Show/Hide"));
    menu->addSeparator();
    auto* quitAction = menu->addAction(tr("Quit"));

    connect(previousAction, &QAction::triggered, this, &TrayIcon::previousRequested);
    connect(playPauseAction_, &QAction::triggered, this, &TrayIcon::playPauseRequested);
    connect(nextAction, &QAction::triggered, this, &TrayIcon::nextRequested);
    connect(stopAction, &QAction::triggered, this, &TrayIcon::stopRequested);
    connect(
        showHideAction, &QAction::triggered, this, [this]() { mainWindow_->setVisible(!mainWindow_->isVisible()); });
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
        Theme::icon(playing ? QStringLiteral("pause") : QStringLiteral("play_arrow"), Theme::IconColor::Ink, 16));
}

void TrayIcon::updateTrayIcon()
{
    // Unknown (no portal/desktop integration reporting a scheme) defaults
    // to the light-panel (black) glyph — a light panel is the more common
    // case, and black-on-unknown is less likely to vanish than white-on-unknown.
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    trayIcon_->setIcon(QIcon(dark ? QStringLiteral(":/icons/icons/tray_icon_dark.svg")
                                  : QStringLiteral(":/icons/icons/tray_icon_light.svg")));
}

} // namespace Integration
