#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QSize>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QSystemTrayIcon>
#include <QTimer>

#include <functional>
#include <memory>

#include "Autostart.h"
#include "Core.h"
#include "Coro.h"
#include "CoverArtCache.h"
#include "CrashReporter.h"
#include "Fonts.h"
#include "GA4Config.h"
#include "GeneratedCoverArt.h"
#include "Logging.h"
#include "MainWindow.h"
#include "NotificationToast.h"
#include "PlaybackController.h"
#include "ProxyRouting.h"
#include "RpcClient.h"
#include "Settings.h"
#include "Settings/GeneralPage.h"
#include "SourceManager.h"
#include "Style.h"
#include "StyleSheet.h"
#include "ThemedToolTip.h"
#include "Tokens.h"
#include "TrayIcon.h"
#include "Typography.h"
#include "Version.h"
#include "WindowGlass.h"
#include "WindowHost.h"

#ifdef Q_OS_WIN
#include <windows.h>

#include <shobjidl.h>

#include "FirstFrameBackground.h"
#include "SmtcService.h"
#include "TaskbarThumbButtons.h"
#else
#include "GlobalShortcuts.h"
#include "MprisService.h"
#endif

namespace {

// Awaits every backend's shutdown() in turn (closeStdin -> terminate ->
// kill fallback, per docs/protocol.md §4.2) before calling done, so
// quitting never leaves an orphan backend subprocess behind.
Rpc::Task<void> shutdownAll(Rpc::SourceManager& sourceManager, std::function<void()> done)
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
    done();
}

#ifdef Q_OS_WIN
// Windows' "Smooth edges of screen fonts" and ClearType settings.
enum class FontSmoothing {
    Off,
    Grayscale,
    ClearTypeRgb,
    ClearTypeBgr,
};

FontSmoothing systemFontSmoothing()
{
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &enabled, 0);
    if (!enabled)
        return FontSmoothing::Off;
    UINT type = 0;
    if (!SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0) || type != FE_FONTSMOOTHINGCLEARTYPE)
        return FontSmoothing::Grayscale;
    UINT orientation = FE_FONTSMOOTHINGORIENTATIONRGB;
    SystemParametersInfoW(SPI_GETFONTSMOOTHINGORIENTATION, 0, &orientation, 0);
    return orientation == FE_FONTSMOOTHINGORIENTATIONBGR ? FontSmoothing::ClearTypeBgr : FontSmoothing::ClearTypeRgb;
}
#endif

} // namespace

int main(int argc, char** argv)
{
    // Before libmpv or any network access reads the environment.
    Net::bypassProxyForLoopback();
#ifdef Q_OS_WIN
    // Before any window exists (Microsoft's own guidance) — ties this
    // process's windows, and Integration::SmtcService's session, to one
    // stable identity, so Explorer can resolve an icon/name for them.
    // Hardware media keys and the lock screen worked without this
    // (SmtcService::pushThumbnail() feeds them the cover directly), but
    // the volume flyout's "Now playing" card (Windows 10) stayed blank —
    // that one specifically needs to resolve the running process back to
    // a registered app. If it's still blank after this: the next step is
    // giving packaging/windows/cloudmus.nsi's CreateShortCut calls the
    // same id as a Start Menu shortcut property (System::Call +
    // IPropertyStore) — untried so far, since this alone may be enough.
    SetCurrentProcessExplicitAppUserModelID(L"CloudMus.CloudMus");
    // FreeType, not Qt's default DirectWrite: at fractional display scales
    // (13px at 125%) DirectWrite's text came out jagged, with uneven stems.
    // FreeType renders the bundled Manrope as on Linux. Left alone if set,
    // so it can still be overridden.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype");
    // Qt's FreeType engine doesn't read the system's ClearType setting: it
    // takes the subpixel layout from this variable, else from a registry
    // key only the ClearType tuner writes — so text was grayscale even
    // with ClearType on. Read once; a change applies after a restart.
    const FontSmoothing fontSmoothing = systemFontSmoothing();
    if (qEnvironmentVariableIsEmpty("QT_SUBPIXEL_AA_TYPE")) {
        if (fontSmoothing == FontSmoothing::ClearTypeRgb)
            qputenv("QT_SUBPIXEL_AA_TYPE", "RGB");
        else if (fontSmoothing == FontSmoothing::ClearTypeBgr)
            qputenv("QT_SUBPIXEL_AA_TYPE", "BGR");
    }
#endif
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
    QApplication::setApplicationVersion(QStringLiteral(CLOUDMUS_VERSION));
    // Must match the "cloudmus-qt.desktop" basename AppRun installs to
    // ~/.local/share/applications/ (see AppRun's own comment) — without a
    // consistent app_id tying the two together, Wayland compositors that
    // can't take a window icon directly from the client (the bundled Qt6
    // in the AppImage predates the xdg-toplevel-icon-v1 protocol) have no
    // way to resolve one from a .desktop file's Icon= key at all.
    QApplication::setDesktopFileName(QStringLiteral("cloudmus-qt"));
    installLogging(); // reads --debug / CLOUDMUS_QT_DEBUG — see Logging.h
    // Next to debug.log. Right after logging, so the report of even an
    // early crash carries the log lines before it.
    Diagnostics::CrashReporter::install({
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/cloudmus/fronts/qt/crashes"),
        QStringLiteral(CLOUDMUS_VERSION),
    });
    for (const QString& report : Diagnostics::CrashReporter::takeNewReports())
        qWarning() << "cloudmus-qt: the last run crashed, report:" << report;
    // Must happen before anything builds a Theme::Typography::font(): Qt
    // resolves/caches a family's available faces the first time a QFont
    // naming it is used, so registering the Manrope weight files late risks
    // a stale substitution sticking around for that first caller.
    Theme::registerApplicationFonts();
    // Registering the font files above only makes Manrope AVAILABLE — it's
    // not the actual default until something requests it. Without this,
    // every widget that never calls Theme::font() explicitly (a
    // QFormLayout's auto-generated row QLabel, QDialogButtonBox, etc.)
    // silently falls back to whatever generic font Qt/the platform picks,
    // never matching the design system.
    QFont appFont = Theme::font(Theme::TextStyle::Body);
#ifdef Q_OS_WIN
    // Font smoothing turned off in the system: no antialiasing either. Every
    // Theme::font() builds on this font, so they all inherit it.
    if (fontSmoothing == FontSmoothing::Off)
        appFont.setStyleStrategy(QFont::NoAntialias);
    // Qt's FreeType engine hints fully on Windows by default, which snaps
    // every glyph's advance to whole pixels: letter gaps came out uneven
    // and jumped by a pixel when a label turned bold (the sidebar's
    // selected item). Vertical-only hinting keeps the font's own advances,
    // placed at subpixel positions — as on Linux with fontconfig's
    // hintslight.
    appFont.setHintingPreference(QFont::PreferVerticalHinting);
#endif
    QApplication::setFont(appFont);
    App::Core core;
    core.analytics().configure(QStringLiteral(CLOUDMUS_GA4_MEASUREMENT_ID), QStringLiteral(CLOUDMUS_VERSION));
    core.analytics().setDebugView(debugLoggingRequested());
    core.analytics().recordLaunch();
    Config::Settings& settings = core.settings();
    Rpc::SourceManager& sourceManager = core.sourceManager();
    Playback::PlaybackController& playback = core.playback();

    // Before glass, whose default depends on it, and the stylesheet.
    Ui::Settings::GeneralPage::applyColorScheme(settings.colorScheme());
    // Before the stylesheet and any window: both are built for glass or
    // not, and a window can't gain an alpha channel once created.
    Theme::setGlassEnabled(Theme::glassWanted(settings.glassBackground())
        && Integration::WindowGlass::support() != Integration::WindowGlass::Support::None);
    Theme::applyGlobalStyleSheet(app);
#ifdef Q_OS_WIN
    static Integration::FirstFrameBackground firstFrameBackground;
    app.installNativeEventFilter(&firstFrameBackground);
#endif
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

    // Non-square covers fitted over their own blur (needs Qt Widgets,
    // which the core library doesn't link).
    core.coverArtCache().setFitter(&Ui::fitCover);
    Ui::WindowHost windowHost(core);

    Integration::TrayIcon tray(windowHost, core.nowPlaying(), core.playlistEditing());
    QObject::connect(&tray, &Integration::TrayIcon::quitRequested, &windowHost, &Ui::WindowHost::quit);

#ifdef Q_OS_WIN
    // Deferred to the event loop's first idle turn, not constructed here
    // and now: SmtcService's first WinRT/COM calls (activating
    // SystemMediaTransportControls, several synchronous round trips) can
    // take a noticeable moment on a cold process, and running them ahead
    // of windowHost.show() below was found to cost the window its DWM
    // open/restore-from-minimize animation — apparently a show() that
    // lands noticeably later than process start no longer reads to
    // Explorer as a fresh launch. unique_ptr, not a value: both need
    // playback/core, not available this early with default construction,
    // and both must still outlive main()'s own return.
    std::unique_ptr<Integration::SmtcService> smtc;
    std::unique_ptr<Integration::TaskbarThumbButtons> taskbarThumbButtons;
    QTimer::singleShot(0, &windowHost, [&]() {
        smtc = std::make_unique<Integration::SmtcService>(playback, core.coverArtCache());
        taskbarThumbButtons = std::make_unique<Integration::TaskbarThumbButtons>(windowHost, playback);
    });
#else
    Integration::MprisService mpris(playback, core.nowPlaying());
    QObject::connect(&mpris, &Integration::MprisService::quitRequested, &windowHost, &Ui::WindowHost::quit);
    QObject::connect(&mpris, &Integration::MprisService::raiseRequested, &windowHost,
        [&windowHost]() { windowHost.bringToFront(); });
#endif

    Integration::NotificationToast notificationToast(tray.systemTrayIcon());
    // The notification carries the track's cover from the UI's own cache.
    // If it's still loading, the notification goes out right away without
    // it and is updated in place once it arrives (while still on screen).
    struct PendingCover {
        QString url;
        QString title;
        QString artists;
    };
    auto pendingCover = std::make_shared<PendingCover>();
    const QSize notificationCoverSize(256, 256);
    Covers::CoverArtCache* coverCache = &core.coverArtCache();
    QObject::connect(&playback, &Playback::PlaybackController::trackChanged, &notificationToast,
        [&notificationToast, &settings, coverCache, pendingCover, notificationCoverSize](
            const Track& track, const QString&) {
            if (!settings.trackNotifications())
                return;
            QString artists;
            for (int i = 0; i < track.artists.size(); ++i) {
                if (i > 0)
                    artists += QStringLiteral(", ");
                artists += track.artists[i].name;
            }
            QString coverUrl = track.coverUrl.value_or(QString());
            if (coverUrl.isEmpty() && track.album.has_value())
                coverUrl = track.album->coverUrl.value_or(QString());
            const QPixmap cover = coverUrl.isEmpty() ? QPixmap() : coverCache->pixmap(coverUrl, notificationCoverSize);
            *pendingCover = { cover.isNull() ? coverUrl : QString(), track.title, artists };
            notificationToast.showTrackChange(track.title, artists, cover);
        });
    // Clicking the notification brings the player window up — out of the
    // tray, from minimized, or from behind other windows.
    QObject::connect(&notificationToast, &Integration::NotificationToast::activated, &windowHost,
        [&windowHost](const QString& activationToken) { windowHost.bringToFront(activationToken); });
    QObject::connect(coverCache, &Covers::CoverArtCache::pixmapReady, &notificationToast,
        [&notificationToast, coverCache, pendingCover, notificationCoverSize](const QString& url) {
            if (pendingCover->url.isEmpty() || url != pendingCover->url)
                return;
            const QPixmap cover = coverCache->pixmap(url, notificationCoverSize);
            if (cover.isNull())
                return; // another size of the same URL landed; ours is still coming
            pendingCover->url.clear();
            notificationToast.updateCover(pendingCover->title, pendingCover->artists, cover);
        });

#ifndef Q_OS_WIN
    Integration::GlobalShortcuts globalShortcuts;
    QObject::connect(&globalShortcuts, &Integration::GlobalShortcuts::playPauseTriggered, &playback,
        &Playback::PlaybackController::togglePause);
    QObject::connect(
        &globalShortcuts, &Integration::GlobalShortcuts::nextTriggered, &playback, &Playback::PlaybackController::next);
    QObject::connect(&globalShortcuts, &Integration::GlobalShortcuts::previousTriggered, &playback,
        &Playback::PlaybackController::previous);
    QObject::connect(
        &globalShortcuts, &Integration::GlobalShortcuts::stopTriggered, &playback, &Playback::PlaybackController::stop);
#endif

    QObject::connect(&windowHost, &Ui::WindowHost::aboutToReallyQuit, &windowHost, [&sourceManager, &core]() {
        // Queued usage events go out while the backends shut down; the app
        // quits once both are done, whichever finishes last.
        auto remaining = std::make_shared<int>(2);
        auto done = [remaining]() {
            if (--*remaining == 0)
                qApp->quit();
        };
        core.analytics().flush(2000, done);
        shutdownAll(sourceManager, done).detach();
    });

    sourceManager.startAll();

    Integration::Autostart::refresh();
    const bool launchedAtLogin
        = app.arguments().contains(QLatin1String(Integration::Autostart::kLaunchedAtLoginArgument));
    if (launchedAtLogin && settings.startHiddenAtLogin()) {
        // At login the panel hosting the tray may come up after us. Wait a
        // while for it rather than showing the window right away — but
        // never stay invisible with no tray icon to bring the window back.
        auto* trayWait = new QTimer(&windowHost);
        trayWait->setInterval(500);
        auto waited = std::make_shared<int>(0);
        QObject::connect(trayWait, &QTimer::timeout, &windowHost, [trayWait, waited, &windowHost]() {
            constexpr int kMaxWaitMs = 10000;
            if (QSystemTrayIcon::isSystemTrayAvailable()) {
                trayWait->deleteLater();
                return;
            }
            *waited += trayWait->interval();
            if (*waited >= kMaxWaitMs) {
                qInfo() << "No system tray after" << kMaxWaitMs << "ms; showing the window";
                trayWait->deleteLater();
                windowHost.show();
            }
        });
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            trayWait->start();
        else
            trayWait->deleteLater();
    } else {
        windowHost.show();
    }

    return QApplication::exec();
}
