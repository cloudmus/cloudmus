#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QtQml/QQmlExtensionPlugin>

#include <memory>

#include "AuraImageProvider.h"
#include "Backend.h"
#include "CoverImageProvider.h"
#include "QrImageProvider.h"
#include "Log.h"
#include "Platform.h"

#ifndef Q_OS_ANDROID
#include "Host.h"
#include "MobileCore.h"
#endif

#ifdef Q_OS_ANDROID
#include "Jni.h"

// The UI library's own: CmLog is found here, on the loading Java thread
// (see Android::Jni::initialize()).
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*)
{
    Android::Jni::initialize(vm);
    return JNI_VERSION_1_6;
}
#endif

Q_IMPORT_QML_PLUGIN(Cloudmus_MobilePlugin)

namespace {

QString dataDir()
{
    // Android: the app's files dir, shared with the playback process
    // (app.cloudmus.mobile.CorePaths).
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString logDir()
{
#ifdef Q_OS_ANDROID
    return dataDir() + QStringLiteral("/logs");
#else
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty())
        state = QDir::homePath() + QStringLiteral("/.local/state");
    return state + QStringLiteral("/cloudmus-mobile/logs");
#endif
}

QString socketPath() { return dataDir() + QStringLiteral("/core.sock"); }

#ifndef Q_OS_ANDROID
App::MobileCore::Paths corePaths()
{
    QString media = qEnvironmentVariable("CLOUDMUS_MOBILE_MEDIA");
    if (media.isEmpty())
        media = QStringLiteral(CLOUDMUS_MOBILE_MEDIA_DIR);
    return { media, dataDir() + QStringLiteral("/core.ini") };
}

// `--core`: the core alone, offered over the socket — what the Android
// playback service runs, as a desktop process for `--split`.
int runCore(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("cloudmus"));
    app.setApplicationName(QStringLiteral("cloudmus-mobile"));
    Logging::install(QStringLiteral("playback"), logDir());
    Logging::mark(QStringLiteral("core process started"));
    QDir().mkpath(dataDir());
    App::MobileCore core(corePaths());
    Remote::Host host(core, socketPath());
    Logging::mark(QStringLiteral("core ready"));
    return app.exec();
}
#endif

// The drawing experiments of Developer tools (Main.qml's uiSettings): they work
// through environment variables Qt reads as the app starts.
void applyRenderingSettings()
{
    QSettings settings;
    const QByteArray renderLoop = settings.value(QStringLiteral("ui/renderLoop")).toByteArray();
    if (!renderLoop.isEmpty() && !qEnvironmentVariableIsSet("QSG_RENDER_LOOP"))
        qputenv("QSG_RENDER_LOOP", renderLoop);
    if (!settings.value(QStringLiteral("ui/touchCompression"), false).toBool())
        qputenv("QML_NO_TOUCH_COMPRESSION", "1");
}

} // namespace

int main(int argc, char* argv[])
{
#ifndef Q_OS_ANDROID
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--core") == 0)
            return runCore(argc, argv);
    }
#endif

    QCoreApplication::setOrganizationName(QStringLiteral("cloudmus"));
    QCoreApplication::setApplicationName(QStringLiteral("cloudmus-mobile"));
    applyRenderingSettings();
    QGuiApplication app(argc, argv);
    QDir().mkpath(dataDir());
    Logging::install(QStringLiteral("ui"), logDir());
    Logging::mark(QStringLiteral("ui main()"));

    std::unique_ptr<Ui::Backend> backend;
#ifdef Q_OS_ANDROID
    // The core lives in the playback service's process; starting the
    // service brings it up (if music isn't playing already).
    Ui::Platform::limitAccessibility();
    Ui::Platform::startService();
    backend = std::make_unique<Ui::Backend>(socketPath());
    backend->setRestartCore(&Ui::Platform::startService);
#else
    std::unique_ptr<App::MobileCore> core;
    std::unique_ptr<QProcess> coreProcess;
    if (app.arguments().contains(QStringLiteral("--split"))) {
        // The Android layout on the desktop: the core in a process of its
        // own, the views on replicas — to debug the remoting here.
        coreProcess = std::make_unique<QProcess>();
        coreProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        coreProcess->start(QCoreApplication::applicationFilePath(), { QStringLiteral("--core") });
        backend = std::make_unique<Ui::Backend>(socketPath());
    } else {
        core = std::make_unique<App::MobileCore>(corePaths());
        backend = std::make_unique<Ui::Backend>(*core);
    }
#endif
    Ui::Platform platform(logDir());

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("aura"), new Ui::AuraImageProvider);
    engine.addImageProvider(QStringLiteral("cover"), new Ui::CoverImageProvider);
    engine.addImageProvider(QStringLiteral("qr"), new Ui::QrImageProvider);
    engine.setInitialProperties({
        { QStringLiteral("backend"), QVariant::fromValue<QObject*>(backend.get()) },
        { QStringLiteral("platform"), QVariant::fromValue<QObject*>(&platform) },
    });
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Cloudmus.Mobile", "Main");
    Logging::mark(QStringLiteral("qml loaded"));

    const int result = app.exec();
#ifdef Q_OS_ANDROID
    // Not through the static destructors: see MainActivity.onDestroy().
    Ui::Platform::exitNow(result);
#else
    if (coreProcess) {
        coreProcess->terminate();
        coreProcess->waitForFinished(3000);
    }
#endif
    return result;
}
