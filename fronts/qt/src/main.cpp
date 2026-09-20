#include <QApplication>
#include <QIcon>
#include <QSize>
#include <QStyleFactory>

#include "Coro.h"
#include "Fonts.h"
#include "GlobalShortcuts.h"
#include "Logging.h"
#include "MainWindow.h"
#include "MprisService.h"
#include "NotificationToast.h"
#include "PlaybackController.h"
#include "RpcClient.h"
#include "Settings.h"
#include "SourceManager.h"
#include "Style.h"
#include "StyleSheet.h"
#include "ThemedToolTip.h"
#include "TrayIcon.h"

namespace {

// Awaits every backend's shutdown() in turn (closeStdin -> terminate ->
// kill fallback, per docs/protocol.md §4.2) before actually exiting the
// process, so quitting never leaves an orphan backend subprocess behind.
Rpc::Task<void> shutdownAllAndQuit(Rpc::SourceManager& sourceManager)
{
    const QList<Rpc::RpcClient*> clients = sourceManager.clients();
    for (Rpc::RpcClient* client : clients) {
        try {
            co_await client->shutdown();
        } catch (const Rpc::RpcCallException&) {
            // best-effort — RpcClient::shutdown() already falls back to
            // terminate/kill regardless of whether the ack arrived.
        }
    }
    qApp->quit();
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    // Fusion, not whatever native style the desktop provides (Breeze under
    // KDE): Breeze's own QCommonStyle-derived painting largely ignores QSS
    // properties like border-radius on QPushButton/QToolButton, which is
    // why the design system's round Icon/Play buttons rendered as plain
    // squares under it — Fusion is Qt's own style and fully respects the
    // stylesheet Theme::StyleSheet generates. Wrapped in CloudMusStyle for
    // the one thing QSS alone can't do: genuinely rounding QMenu (see
    // Style.h) — every other widget still renders exactly as plain Fusion
    // would.
    QApplication::setStyle(new Theme::CloudMusStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    QApplication::setOrganizationName(QStringLiteral("cloudmus"));
    QApplication::setApplicationName(QStringLiteral("cloudmus-qt"));
    // Must match the "cloudmus-qt.desktop" basename AppRun installs to
    // ~/.local/share/applications/ (see AppRun's own comment) — without a
    // consistent app_id tying the two together, Wayland compositors that
    // can't take a window icon directly from the client (the bundled Qt6
    // in the AppImage predates the xdg-toplevel-icon-v1 protocol) have no
    // way to resolve one from a .desktop file's Icon= key at all.
    QApplication::setDesktopFileName(QStringLiteral("cloudmus-qt"));
    installLogging(); // reads --debug / CLOUDMUS_QT_DEBUG — see Logging.h
    // Must happen before anything builds a Theme::Typography::font(): Qt
    // resolves/caches a family's available faces the first time a QFont
    // naming it is used, so registering the Manrope weight files late risks
    // a stale substitution sticking around for that first caller.
    Theme::registerApplicationFonts();
    Theme::applyGlobalStyleSheet(app);
    new Ui::ThemedToolTip(&app); // global service, not tied to any specific widget — see its own class doc
    // A bare-SVG QIcon lets Qt's SVG engine render sharply at whatever
    // size is *requested*, but under X11 (including XWayland) Qt still
    // has to bake a handful of concrete raster sizes into the
    // _NET_WM_ICON property — Alt+Tab/Present Windows-style switchers
    // commonly read that property directly, unlike the taskbar/task
    // manager widget (which resolves the icon via the .desktop file's
    // Icon= key instead, matched through StartupWMClass/setDesktopFileName
    // above) — confirmed in practice: the taskbar icon updated correctly
    // on its own, but Alt+Tab kept showing a soft/blurry one. Without an
    // explicit large size, whatever modest default Qt bakes in gets
    // upscaled for Alt+Tab's bigger preview. Adding the already-bundled
    // 512px PNG explicitly guarantees a genuinely sharp large variant.
    QIcon appIcon(QStringLiteral(":/icons/icons/logo.svg"));
    appIcon.addFile(QStringLiteral(":/icons/icons/logo.png"), QSize(512, 512));
    QApplication::setWindowIcon(appIcon);
    QApplication::setQuitOnLastWindowClosed(false); // closing to tray must not exit the app

    Config::Settings settings;
    Rpc::SourceManager sourceManager;
    Playback::PlaybackController playback(sourceManager);

    Ui::MainWindow window(sourceManager, playback, settings);

    Integration::TrayIcon tray(&window);
    QObject::connect(
        &tray, &Integration::TrayIcon::previousRequested, &playback, &Playback::PlaybackController::previous);
    QObject::connect(
        &tray, &Integration::TrayIcon::playPauseRequested, &playback, &Playback::PlaybackController::togglePause);
    QObject::connect(&tray, &Integration::TrayIcon::nextRequested, &playback, &Playback::PlaybackController::next);
    QObject::connect(&tray, &Integration::TrayIcon::stopRequested, &playback, &Playback::PlaybackController::stop);
    QObject::connect(&tray, &Integration::TrayIcon::quitRequested, &window, &Ui::MainWindow::quitForReal);
    QObject::connect(
        &playback, &Playback::PlaybackController::trackChanged, &tray, [&tray](const Track& track, const QString&) {
            QString artists;
            for (int i = 0; i < track.artists.size(); ++i) {
                if (i > 0)
                    artists += QStringLiteral(", ");
                artists += track.artists[i].name;
            }
            tray.setNowPlayingTooltip(track.title, artists);
        });
    QObject::connect(
        &playback, &Playback::PlaybackController::playingChanged, &tray, &Integration::TrayIcon::setPlaying);

    Integration::MprisService mpris(&window, playback);
    QObject::connect(&mpris, &Integration::MprisService::quitRequested, &window, &Ui::MainWindow::quitForReal);

    Integration::NotificationToast notificationToast;
    QObject::connect(&playback, &Playback::PlaybackController::trackChanged, &notificationToast,
        [&notificationToast](const Track& track, const QString&) {
            QString artists;
            for (int i = 0; i < track.artists.size(); ++i) {
                if (i > 0)
                    artists += QStringLiteral(", ");
                artists += track.artists[i].name;
            }
            notificationToast.showTrackChange(track.title, artists, QPixmap());
        });

    Integration::GlobalShortcuts globalShortcuts;
    QObject::connect(&globalShortcuts, &Integration::GlobalShortcuts::playPauseTriggered, &playback,
        &Playback::PlaybackController::togglePause);
    QObject::connect(
        &globalShortcuts, &Integration::GlobalShortcuts::nextTriggered, &playback, &Playback::PlaybackController::next);
    QObject::connect(&globalShortcuts, &Integration::GlobalShortcuts::previousTriggered, &playback,
        &Playback::PlaybackController::previous);
    QObject::connect(
        &globalShortcuts, &Integration::GlobalShortcuts::stopTriggered, &playback, &Playback::PlaybackController::stop);

    QObject::connect(&window, &Ui::MainWindow::aboutToReallyQuit, &window,
        [&sourceManager]() { shutdownAllAndQuit(sourceManager).detach(); });

    sourceManager.startAll();
    window.show();

    return QApplication::exec();
}
