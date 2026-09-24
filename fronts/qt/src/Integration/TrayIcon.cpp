#include "TrayIcon.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QWidget>

#include "Icons.h"
#include "MainWindow.h"

namespace Integration {

TrayIcon::TrayIcon(Ui::MainWindow* mainWindow, QObject* parent)
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
    feedbackSeparator_ = menu->addSeparator();
    likeAction_ = menu->addAction(QString());
    dislikeAction_ = menu->addAction(QString());
    playlistsMenu_
        = menu->addMenu(Theme::icon(QStringLiteral("playlist_add"), Theme::IconColor::Ink, 16), tr("Playlists"));
    menu->addSeparator();
    showHideAction_ = menu->addAction(QString());
    menu->addSeparator();
    auto* quitAction = menu->addAction(tr("Quit"));

    connect(previousAction, &QAction::triggered, this, &TrayIcon::previousRequested);
    connect(playPauseAction_, &QAction::triggered, this, &TrayIcon::playPauseRequested);
    connect(nextAction, &QAction::triggered, this, &TrayIcon::nextRequested);
    connect(stopAction, &QAction::triggered, this, &TrayIcon::stopRequested);
    connect(likeAction_, &QAction::triggered, this,
        [this]() { mainWindow_->setNowPlayingLiked(!mainWindow_->nowPlayingFeedback().liked); });
    connect(dislikeAction_, &QAction::triggered, this,
        [this]() { mainWindow_->setNowPlayingDisliked(!mainWindow_->nowPlayingFeedback().disliked); });
    // Refilled on every open: membership may have changed since (in the
    // app, or on the service itself).
    connect(playlistsMenu_, &QMenu::aboutToShow, this,
        [this]() { mainWindow_->fillNowPlayingPlaylistsMenu(playlistsMenu_); });
    connect(showHideAction_, &QAction::triggered, mainWindow_, &Ui::MainWindow::toggleShown);
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    connect(mainWindow_, &Ui::MainWindow::nowPlayingFeedbackChanged, this, &TrayIcon::refreshFeedbackActions);
    refreshFeedbackActions();
    // The window's state can change without the tray (minimized from its
    // title bar, closed to the tray) — relabel just before showing.
    connect(menu, &QMenu::aboutToShow, this, &TrayIcon::refreshShowHideAction);
    refreshShowHideAction();

    trayIcon_->setContextMenu(menu);

    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            mainWindow_->toggleShown();
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

void TrayIcon::refreshFeedbackActions()
{
    const Ui::MainWindow::NowPlayingFeedback feedback = mainWindow_->nowPlayingFeedback();
    // Same look as the track context menu's: state by the icon, not a check.
    likeAction_->setVisible(feedback.likeSupported);
    likeAction_->setText(feedback.liked ? tr("Unlike") : tr("Like"));
    likeAction_->setIcon(feedback.liked ? Theme::icon(QStringLiteral("favorite"), Theme::IconColor::Accent, 16)
                                        : Theme::icon(QStringLiteral("favorite_border"), Theme::IconColor::Ink, 16));
    dislikeAction_->setVisible(feedback.dislikeSupported);
    dislikeAction_->setText(feedback.disliked ? tr("Remove Dislike") : tr("Dislike"));
    dislikeAction_->setIcon(Theme::icon(
        QStringLiteral("heart_broken"), feedback.disliked ? Theme::IconColor::Accent : Theme::IconColor::Ink, 16));
    playlistsMenu_->menuAction()->setVisible(feedback.playlistsSupported);
    feedbackSeparator_->setVisible(feedback.likeSupported || feedback.dislikeSupported || feedback.playlistsSupported);
}

void TrayIcon::refreshShowHideAction()
{
    const bool onScreen = mainWindow_->isOnScreen();
    showHideAction_->setText(onScreen ? tr("Hide") : tr("Show"));
    showHideAction_->setIcon(Theme::icon(
        onScreen ? QStringLiteral("visibility_off") : QStringLiteral("visibility"), Theme::IconColor::Ink, 16));
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
