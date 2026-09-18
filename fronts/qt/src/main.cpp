#include <QApplication>
#include <QIcon>

#include "Coro.h"
#include "GlobalShortcuts.h"
#include "Logging.h"
#include "MainWindow.h"
#include "MprisService.h"
#include "NotificationToast.h"
#include "PlaybackController.h"
#include "RpcClient.h"
#include "Settings.h"
#include "SourceManager.h"
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
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/icons/logo.svg")));
    QApplication::setQuitOnLastWindowClosed(false); // closing to tray must not exit the app

    Config::Settings settings;
    Rpc::SourceManager sourceManager;
    Playback::PlaybackController playback(sourceManager);

    Ui::MainWindow window(sourceManager, playback, settings);

    Integration::TrayIcon tray(&window);
    QObject::connect(&tray, &Integration::TrayIcon::previousRequested, &playback,
                     &Playback::PlaybackController::previous);
    QObject::connect(&tray, &Integration::TrayIcon::playPauseRequested, &playback,
                     &Playback::PlaybackController::togglePause);
    QObject::connect(&tray, &Integration::TrayIcon::nextRequested, &playback, &Playback::PlaybackController::next);
    QObject::connect(&tray, &Integration::TrayIcon::stopRequested, &playback, &Playback::PlaybackController::stop);
    QObject::connect(&tray, &Integration::TrayIcon::quitRequested, &window, &Ui::MainWindow::quitForReal);
    QObject::connect(&playback, &Playback::PlaybackController::trackChanged, &tray,
                     [&tray](const Track& track, const QString&) {
                         QString artists;
                         for (int i = 0; i < track.artists.size(); ++i) {
                             if (i > 0)
                                 artists += QStringLiteral(", ");
                             artists += track.artists[i].name;
                         }
                         tray.setNowPlayingTooltip(track.title, artists);
                     });
    QObject::connect(&playback, &Playback::PlaybackController::playingChanged, &tray,
                     &Integration::TrayIcon::setPlaying);

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
    QObject::connect(&globalShortcuts, &Integration::GlobalShortcuts::nextTriggered, &playback,
                     &Playback::PlaybackController::next);
    QObject::connect(&globalShortcuts, &Integration::GlobalShortcuts::previousTriggered, &playback,
                     &Playback::PlaybackController::previous);
    QObject::connect(&globalShortcuts, &Integration::GlobalShortcuts::stopTriggered, &playback,
                     &Playback::PlaybackController::stop);

    QObject::connect(&window, &Ui::MainWindow::aboutToReallyQuit, &window,
                     [&sourceManager]() { shutdownAllAndQuit(sourceManager).detach(); });

    sourceManager.startAll();
    window.show();

    return QApplication::exec();
}
