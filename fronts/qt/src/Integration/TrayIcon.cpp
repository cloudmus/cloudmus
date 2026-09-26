#include "TrayIcon.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QWidget>

#include "Icons.h"
#include "NowPlaying.h"
#include "PlaylistEditing.h"
#include "WindowHost.h"

namespace Integration {

TrayIcon::TrayIcon(Ui::WindowHost& windowHost, ViewModel::NowPlaying& nowPlaying, App::PlaylistEditing& playlistEditing,
    QObject* parent)
    : QObject(parent)
    , windowHost_(windowHost)
    , nowPlaying_(nowPlaying)
    , playlistEditing_(playlistEditing)
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

    using ViewModel::NowPlaying;
    connect(previousAction, &QAction::triggered, &nowPlaying_, &NowPlaying::previous);
    connect(playPauseAction_, &QAction::triggered, &nowPlaying_, &NowPlaying::togglePause);
    connect(nextAction, &QAction::triggered, &nowPlaying_, &NowPlaying::next);
    connect(stopAction, &QAction::triggered, &nowPlaying_, &NowPlaying::stop);
    connect(likeAction_, &QAction::triggered, this, [this]() { nowPlaying_.setLiked(!nowPlaying_.feedback().liked); });
    connect(dislikeAction_, &QAction::triggered, this,
        [this]() { nowPlaying_.setDisliked(!nowPlaying_.feedback().disliked); });
    // Refilled on every open: membership may have changed since (in the
    // app, or on the service itself).
    connect(playlistsMenu_, &QMenu::aboutToShow, this, &TrayIcon::fillPlaylistsMenu);
    connect(showHideAction_, &QAction::triggered, &windowHost_, &Ui::WindowHost::toggleShown);
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    connect(&nowPlaying_, &NowPlaying::feedbackChanged, this, &TrayIcon::refreshFeedbackActions);
    connect(&nowPlaying_, &NowPlaying::trackChanged, this, &TrayIcon::refreshTrack);
    connect(&nowPlaying_, &NowPlaying::playingChanged, this, &TrayIcon::refreshPlaying);
    refreshFeedbackActions();
    refreshTrack();
    refreshPlaying();
    // The window's state can change without the tray (minimized from its
    // title bar, closed to the tray) — relabel just before showing.
    connect(menu, &QMenu::aboutToShow, this, &TrayIcon::refreshShowHideAction);
    refreshShowHideAction();

    trayIcon_->setContextMenu(menu);

    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            windowHost_.toggleShown();
    });

    trayIcon_->show();
}

void TrayIcon::refreshTrack()
{
    // Kept while nothing plays, like before — it names the last track.
    if (!nowPlaying_.hasTrack())
        return;
    const Track& track = nowPlaying_.track();
    QString artists;
    for (int i = 0; i < track.artists.size(); ++i) {
        if (i > 0)
            artists += QStringLiteral(", ");
        artists += track.artists[i].name;
    }
    trayIcon_->setToolTip(artists.isEmpty() ? track.title : QStringLiteral("%1 — %2").arg(track.title, artists));
}

void TrayIcon::refreshPlaying()
{
    const bool playing = nowPlaying_.playing();
    playPauseAction_->setText(playing ? tr("Pause") : tr("Play"));
    playPauseAction_->setIcon(
        Theme::icon(playing ? QStringLiteral("pause") : QStringLiteral("play_arrow"), Theme::IconColor::Ink, 16));
}

void TrayIcon::refreshFeedbackActions()
{
    const ViewModel::NowPlaying::Feedback feedback = nowPlaying_.feedback();
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

void TrayIcon::fillPlaylistsMenu()
{
    playlistsMenu_->clear();
    if (!nowPlaying_.hasTrack())
        return;
    playlistsMenu_->addAction(tr("Loading…"))->setEnabled(false);
    // Copies: the track may change while membership loads.
    [](TrayIcon* self, QPointer<QMenu> menu, QString sourceId, Track track) -> Rpc::Task<void> {
        const auto membership = co_await self->playlistEditing_.membership(sourceId, track.id);
        if (!menu)
            co_return;
        menu->clear();
        if (!membership || membership->playlists.isEmpty()) {
            menu->addAction(membership ? tr("No playlists") : tr("Couldn't load playlists"))->setEnabled(false);
            co_return;
        }
        for (const Playlist& playlist : membership->playlists) {
            QAction* action = menu->addAction(playlist.title);
            action->setCheckable(true);
            action->setChecked(membership->containing.contains(playlist.id));
            QObject::connect(action, &QAction::toggled, self, [self, sourceId, track, playlist](bool checked) {
                self->playlistEditing_.setTrackInPlaylist(sourceId, track, playlist, checked).detach();
            });
        }
    }(this, playlistsMenu_, nowPlaying_.sourceId(), nowPlaying_.track())
                                                                                   .detach();
}

void TrayIcon::refreshShowHideAction()
{
    const bool onScreen = windowHost_.isOnScreen();
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
