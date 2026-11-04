#include <QCoreApplication>
#include <QDir>
#include <QImageWriter>
#include <QMetaObject>
#include <QSslSocket>
#include <QUrl>

#include <functional>
#include <jni.h>
#include <mutex>
#include <thread>
#include <vector>

#include "Host.h"
#include "Jni.h"
#include "Log.h"
#include "MobileCore.h"

// The core as the playback service runs it: a library loaded by
// CoreBridge.java into the :playback process, with Qt Core and no Qt
// platform plugin (so no Gui). The core lives on a thread of its own with
// its own event loop; Java's commands are posted to it, and its state is
// reported back through CoreBridge.onState().

extern "C" int av_jni_set_java_vm(void* vm, void* logCtx);

namespace {

enum Command {
    Play = 1,
    Pause,
    Next,
    Previous,
    Seek
};

jclass bridgeClass = nullptr;
jmethodID onStateMethod = nullptr;
jmethodID onTrackMethod = nullptr;
jmethodID onAutoplayMethod = nullptr;
jmethodID onResumeAfterLossMethod = nullptr;
jmethodID onReadyMethod = nullptr;
jmethodID onStoppedMethod = nullptr;

std::mutex mutex;
QObject* receiver = nullptr; // lives in the core thread, once it's up
std::vector<std::function<void()>> pending; // commands that came before that
std::thread coreThread;
App::MobileCore* coreInstance = nullptr; // only touched in the core thread

void post(std::function<void()> action)
{
    std::lock_guard lock(mutex);
    if (!receiver) {
        pending.push_back(std::move(action));
        return;
    }
    QMetaObject::invokeMethod(receiver, std::move(action), Qt::QueuedConnection);
}

void reportState(App::MobileCore& core)
{
    JNIEnv* env = Android::Jni::env();
    auto& now = core.nowPlaying();
    env->CallStaticVoidMethod(bridgeClass, onStateMethod, jboolean(now.playing()), jboolean(now.loading()),
        jlong(now.positionMs()), jlong(now.durationMs()));
    Android::Jni::checkException(env, "CoreBridge.onState");
}

jstring toJava(JNIEnv* env, const QString& text)
{
    return env->NewString(reinterpret_cast<const jchar*>(text.constData()), jsize(text.size()));
}

// What the system shows of the track: the notification, the car's widget.
void reportTrack(App::MobileCore& core)
{
    JNIEnv* env = Android::Jni::env();
    auto& now = core.nowPlaying();
    const bool has = now.hasTrack();
    const Track* track = has ? &now.track() : nullptr;
    const QString id = has ? now.sourceId() + QLatin1Char('/') + track->id : QString();
    const QString album = has && track->album ? track->album->title : QString();
    const jstring ids[] = { toJava(env, id), toJava(env, now.title()), toJava(env, now.artist()), toJava(env, album),
        toJava(env, QUrl(now.coverUrl()).toLocalFile()) };
    env->CallStaticVoidMethod(
        bridgeClass, onTrackMethod, ids[0], ids[1], ids[2], ids[3], jlong(now.durationMs()), ids[4]);
    Android::Jni::checkException(env, "CoreBridge.onTrack");
    for (const jstring text : ids)
        env->DeleteLocalRef(text);
}

// What the backends find around them. Their Python and data are the app's,
// and these reach them through this process' environment.
void setUpBackends(const QString& filesDir, const QString& cacheDir, const QString& pythonHome, const QString& libraryDir)
{
    // Qt's plugins (TLS, JPEG for the covers) are with the other libraries.
    // Qt's own activity tells it so; this process doesn't have one.
    QCoreApplication::addLibraryPath(libraryDir);
    const auto set = [](const char* name, const QString& value) { qputenv(name, value.toUtf8()); };
    set("HOME", filesDir);
    set("XDG_CONFIG_HOME", filesDir + QStringLiteral("/config"));
    set("XDG_CACHE_HOME", cacheDir);
    set("TMPDIR", cacheDir);
    // Qt's own HTTPS (streams, covers): the OpenSSL that came with Python.
    set("ANDROID_OPENSSL_SUFFIX", QStringLiteral("_python"));
    if (pythonHome.isEmpty())
        return;
    set("LD_LIBRARY_PATH", libraryDir);
    set("PYTHONHOME", pythonHome);
    set("PYTHONPATH", pythonHome + QStringLiteral("/site-packages"));
    set("SSL_CERT_FILE", pythonHome + QStringLiteral("/site-packages/certifi/cacert.pem"));
    set("CLOUDMUS_BACKENDS_DIR", pythonHome + QStringLiteral("/backends"));
    set("CLOUDMUS_BACKENDS_PYTHON", libraryDir + QStringLiteral("/libcmpython.so"));
}

void runCore(QString filesDir, QString cacheDir, QString pythonHome, QString libraryDir)
{
    static int argc = 1;
    static char name[] = "cloudmus-core";
    static char* argv[] = { name, nullptr };

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("cloudmus"));
    QCoreApplication::setApplicationName(QStringLiteral("cloudmus-mobile"));
    Logging::install(QStringLiteral("playback"), QString());
    Logging::mark(QStringLiteral("core thread started"));

    setUpBackends(filesDir, cacheDir, pythonHome, libraryDir);
    // A second for the UI to come up on what's saved; a few more before the
    // saved lists are refreshed, behind the music that has started by then.
    App::MobileCore core({ QString(), filesDir + QStringLiteral("/core.ini"), 1000, 5000 });
    coreInstance = &core;
    // Without it no stream or cover loads, and nothing else says why.
    qInfo() << "TLS:" << QSslSocket::activeBackend() << QSslSocket::supportsSsl()
            << QSslSocket::sslLibraryVersionString();
    qInfo() << "image formats:" << QImageWriter::supportedImageFormats();
    Remote::Host host(core, filesDir + QStringLiteral("/core.sock"));
    Logging::mark(QStringLiteral("core: remote host listening"));

    auto report = [&core] { reportState(core); };
    auto& now = core.nowPlaying();
    auto track = [&core] { reportTrack(core); };
    QObject::connect(&now, &ViewModel::NowPlaying::trackChanged, &core, track);
    QObject::connect(&now, &ViewModel::NowPlaying::coverUrlChanged, &core, track);
    QObject::connect(&now, &ViewModel::NowPlaying::durationMsChanged, &core, report);
    QObject::connect(&now, &ViewModel::NowPlaying::playingChanged, &core, report);
    QObject::connect(&now, &ViewModel::NowPlaying::loadingChanged, &core, report);
    QObject::connect(&now, &ViewModel::NowPlaying::positionMsChanged, &core, report);
    QObject::connect(&now, &ViewModel::NowPlaying::stopped, &core, [] {
        JNIEnv* env = Android::Jni::env();
        env->CallStaticVoidMethod(bridgeClass, onStoppedMethod);
        Android::Jni::checkException(env, "CoreBridge.onStopped");
    });
    core.settings().onAutoplayChanged = [](bool enabled) {
        JNIEnv* env = Android::Jni::env();
        env->CallStaticVoidMethod(bridgeClass, onAutoplayMethod, jboolean(enabled));
        Android::Jni::checkException(env, "CoreBridge.onAutoplayChanged");
    };
    core.settings().onResumeAfterLossSecChanged = [](int seconds) {
        JNIEnv* env = Android::Jni::env();
        env->CallStaticVoidMethod(bridgeClass, onResumeAfterLossMethod, jint(seconds));
        Android::Jni::checkException(env, "CoreBridge.onResumeAfterLossChanged");
    };

    QObject context;
    {
        std::lock_guard lock(mutex);
        receiver = &context;
        for (auto& action : pending)
            QMetaObject::invokeMethod(&context, std::move(action), Qt::QueuedConnection);
        pending.clear();
    }
    reportTrack(core);
    reportState(core);
    JNIEnv* env = Android::Jni::env();
    env->CallStaticVoidMethod(bridgeClass, onReadyMethod);
    Android::Jni::checkException(env, "CoreBridge.onCoreReady");

    app.exec();

    std::lock_guard lock(mutex);
    receiver = nullptr;
    coreInstance = nullptr;
}

QString toQString(JNIEnv* env, jstring text)
{
    const jchar* chars = env->GetStringChars(text, nullptr);
    QString result(reinterpret_cast<const QChar*>(chars), env->GetStringLength(text));
    env->ReleaseStringChars(text, chars);
    return result;
}

} // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*)
{
    Android::Jni::initialize(vm);
    // libmpv's (and FFmpeg's) Android parts find the VM here.
    av_jni_set_java_vm(vm, nullptr);
    JNIEnv* env = Android::Jni::env();
    jclass local = env->FindClass("dev/cloudmus/mobile/CoreBridge");
    bridgeClass = static_cast<jclass>(env->NewGlobalRef(local));
    onStateMethod = env->GetStaticMethodID(bridgeClass, "onState", "(ZZJJ)V");
    onTrackMethod = env->GetStaticMethodID(bridgeClass, "onTrack",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JLjava/lang/String;)V");
    onAutoplayMethod = env->GetStaticMethodID(bridgeClass, "onAutoplayChanged", "(Z)V");
    onResumeAfterLossMethod = env->GetStaticMethodID(bridgeClass, "onResumeAfterLossChanged", "(I)V");
    onReadyMethod = env->GetStaticMethodID(bridgeClass, "onCoreReady", "()V");
    onStoppedMethod = env->GetStaticMethodID(bridgeClass, "onStopped", "()V");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL Java_dev_cloudmus_mobile_CoreBridge_nativeStart(
    JNIEnv* env, jclass, jstring filesDir, jstring cacheDir, jstring pythonHome, jstring libraryDir)
{
    coreThread = std::thread(runCore, toQString(env, filesDir), toQString(env, cacheDir), toQString(env, pythonHome),
        toQString(env, libraryDir));
}

extern "C" JNIEXPORT void JNICALL Java_dev_cloudmus_mobile_CoreBridge_nativeCommand(
    JNIEnv*, jclass, jint command, jlong argument)
{
    post([command, argument] {
        if (!coreInstance) {
            qWarning() << "system command" << command << "with no core to take it";
            return;
        }
        qInfo() << "system command" << command << argument;
        auto& now = coreInstance->nowPlaying();
        switch (command) {
            case Play:
                now.play();
                break;
            case Pause:
                now.pause();
                break;
            case Next:
                now.next();
                break;
            case Previous:
                now.previous();
                break;
            case Seek:
                now.seek(argument);
                break;
            default:
                break;
        }
    });
}

extern "C" JNIEXPORT void JNICALL Java_dev_cloudmus_mobile_CoreBridge_nativeStop(JNIEnv*, jclass)
{
    post([] { QCoreApplication::quit(); });
}
