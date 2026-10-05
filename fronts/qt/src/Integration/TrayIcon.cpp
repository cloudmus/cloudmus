#include "TrayIcon.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QWidget>

#include <functional>

#include "Icons.h"
#include "NowPlaying.h"
#include "PlaylistEditing.h"
#include "Registry.h"
#include "WindowHost.h"

#ifdef Q_OS_WIN
#include <QSettings>
#include <QWinEventNotifier>

#include <windows.h>
#endif

namespace Integration {

#ifdef Q_OS_WIN
namespace {

constexpr wchar_t kPersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

// The taskbar's own scheme — "Choose your default Windows mode" — apart
// from the apps' one Qt reports (and the app may override). Missing
// before Windows 10 1903, whose taskbar was always dark.
bool taskbarIsLight()
{
    const QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\") + QString::fromWCharArray(kPersonalizeKey), QSettings::NativeFormat);
    return personalize.value(QStringLiteral("SystemUsesLightTheme"), 0).toInt() != 0;
}

// Calls `changed` whenever a value under the Personalize key changes —
// Qt's colorSchemeChanged only follows the apps' scheme.
void watchPersonalizeKey(QObject* owner, std::function<void()> changed)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, 0, KEY_NOTIFY, &key) != ERROR_SUCCESS)
        return;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const auto arm = [key, event]() { RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, event, TRUE); };
    arm();
    auto* notifier = new QWinEventNotifier(event, owner);
    QObject::connect(notifier, &QWinEventNotifier::activated, owner, [arm, changed]() {
        arm(); // a notification fires once
        changed();
    });
    QObject::connect(notifier, &QObject::destroyed, [key, event]() {
        RegCloseKey(key);
        CloseHandle(event);
    });
}

} // namespace
#else
namespace {

// GNOME's top bar (Ubuntu's included) is dark whatever the app theme is, so
// the apps' scheme Qt reports says nothing about it. Other desktops' panels
// follow the theme.
bool panelIsAlwaysDark()
{
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    for (const char* name : { "GNOME", "Unity", "ubuntu" }) {
        if (desktop.contains(QLatin1String(name), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

} // namespace
#endif

TrayIcon::TrayIcon(Ui::WindowHost& windowHost, ViewModel::NowPlaying& nowPlaying, App::PlaylistEditing& playlistEditing,
    Hotkeys::Registry& hotkeys, QObject* parent)
    : QObject(parent)
    , windowHost_(windowHost)
    , nowPlaying_(nowPlaying)
    , playlistEditing_(playlistEditing)
    , hotkeys_(hotkeys)
{
    trayIcon_ = new QSystemTrayIcon(this);
    updateTrayIcon();
#ifdef Q_OS_WIN
    watchPersonalizeKey(this, [this]() { updateTrayIcon(); });
#else
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &TrayIcon::updateTrayIcon);
#endif
    trayIcon_->setToolTip(QStringLiteral("CloudMus"));

    // Context-menu action icons are in scope for the Material Icons
    // migration (unlike the tray glyph itself, updateTrayIcon()'s brand
    // asset, untouched below) — a QMenu popup gets the design system's
    // Panel treatment via Theme::StyleSheet's global QMenu rule.
    auto* menu = new QMenu();
    previousAction_ = menu->addAction(tr("Previous"));
    playPauseAction_ = menu->addAction(tr("Play"));
    nextAction_ = menu->addAction(tr("Next"));
    stopAction_ = menu->addAction(tr("Stop"));
    feedbackSeparator_ = menu->addSeparator();
    likeAction_ = menu->addAction(QString());
    dislikeAction_ = menu->addAction(QString());
    playlistsMenu_ = menu->addMenu(tr("Playlists"));
    menu->addSeparator();
    showHideAction_ = menu->addAction(QString());
    menu->addSeparator();
    quitAction_ = menu->addAction(tr("Quit"));

    using ViewModel::NowPlaying;
    connect(previousAction_, &QAction::triggered, &nowPlaying_, &NowPlaying::previous);
    connect(playPauseAction_, &QAction::triggered, &nowPlaying_, &NowPlaying::togglePause);
    connect(nextAction_, &QAction::triggered, &nowPlaying_, &NowPlaying::next);
    connect(stopAction_, &QAction::triggered, &nowPlaying_, &NowPlaying::stop);
    connect(likeAction_, &QAction::triggered, this, [this]() { nowPlaying_.setLiked(!nowPlaying_.feedback().liked); });
    connect(dislikeAction_, &QAction::triggered, this,
        [this]() { nowPlaying_.setDisliked(!nowPlaying_.feedback().disliked); });
    // Refilled on every open: membership may have changed since (in the
    // app, or on the service itself).
    connect(playlistsMenu_, &QMenu::aboutToShow, this, &TrayIcon::fillPlaylistsMenu);
    connect(showHideAction_, &QAction::triggered, &windowHost_, &Ui::WindowHost::toggleShown);
    connect(quitAction_, &QAction::triggered, this, &TrayIcon::quitRequested);

    connect(&hotkeys_, &Hotkeys::Registry::bindingsChanged, this, &TrayIcon::refreshHotkeys);
    refreshHotkeys();
    connect(&nowPlaying_, &NowPlaying::feedbackChanged, this, &TrayIcon::refreshFeedbackActions);
    connect(&nowPlaying_, &NowPlaying::trackChanged, this, &TrayIcon::refreshTrack);
    connect(&nowPlaying_, &NowPlaying::playingChanged, this, &TrayIcon::refreshPlaying);
    // The rest of the icons follow state, refreshed by the calls below.
    Theme::followTheme(menu, [this]() {
        previousAction_->setIcon(Theme::icon(QStringLiteral("skip_previous"), Theme::IconColor::Ink, 16));
        nextAction_->setIcon(Theme::icon(QStringLiteral("skip_next"), Theme::IconColor::Ink, 16));
        stopAction_->setIcon(Theme::icon(QStringLiteral("stop"), Theme::IconColor::Ink, 16));
        playlistsMenu_->setIcon(Theme::icon(QStringLiteral("playlist_add"), Theme::IconColor::Ink, 16));
        refreshFeedbackActions();
        refreshPlaying();
        refreshShowHideAction();
    });
    refreshTrack();
    // The window's state can change without the tray (minimized from its
    // title bar, closed to the tray) — relabel just before showing.
    connect(menu, &QMenu::aboutToShow, this, &TrayIcon::refreshShowHideAction);

    trayIcon_->setContextMenu(menu);
    // QCoreApplication posts LanguageChange to itself when a translator is installed.
    qApp->installEventFilter(this);

    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            windowHost_.toggleShown();
    });

    trayIcon_->show();
}

bool TrayIcon::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp && event->type() == QEvent::LanguageChange)
        retranslate();
    return QObject::eventFilter(watched, event);
}

void TrayIcon::retranslate()
{
    previousAction_->setText(tr("Previous"));
    nextAction_->setText(tr("Next"));
    stopAction_->setText(tr("Stop"));
    playlistsMenu_->setTitle(tr("Playlists"));
    quitAction_->setText(tr("Quit"));
    // The state-dependent labels, and the hotkeys shown next to them.
    refreshPlaying();
    refreshFeedbackActions();
    refreshShowHideAction();
    refreshHotkeys();
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

void TrayIcon::refreshHotkeys()
{
    using Hotkeys::Action;
    const std::pair<QAction*, Action> items[] = { { previousAction_, Action::Previous },
        { playPauseAction_, Action::PlayPause }, { nextAction_, Action::Next }, { stopAction_, Action::Stop },
        { likeAction_, Action::Like }, { dislikeAction_, Action::Dislike }, { showHideAction_, Action::ShowPlayer } };
    for (const auto& [item, action] : items)
        Hotkeys::showKey(item, hotkeys_.binding(action).key);
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
    dislikeAction_->setIcon(
        Theme::icon(feedback.disliked ? QStringLiteral("heart_off") : QStringLiteral("heart_off_outline"),
            feedback.disliked ? Theme::IconColor::Accent : Theme::IconColor::Ink, 16));
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
#ifdef Q_OS_WIN
    // The icon sits on the taskbar, which has a scheme of its own: a dark
    // taskbar with light apps is Windows' default.
    const bool dark = !taskbarIsLight();
#else
    const bool dark = panelIsAlwaysDark() || QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#endif
    trayIcon_->setIcon(QIcon(dark ? QStringLiteral(":/icons/icons/tray_icon_dark.svg")
                                  : QStringLiteral(":/icons/icons/tray_icon_light.svg")));
}

} // namespace Integration
