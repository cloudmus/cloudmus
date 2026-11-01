#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>

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
    Seek,
    PlayIndex
};

jclass bridgeClass = nullptr;
jmethodID onStateMethod = nullptr;
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
    env->CallStaticVoidMethod(bridgeClass, onStateMethod, jint(now.currentIndex()), jboolean(now.playing()),
        jboolean(now.loading()), jlong(now.positionMs()), jlong(now.durationMs()));
    Android::Jni::checkException(env, "CoreBridge.onState");
}

void runCore(QString filesDir, QString mediaDir)
{
    static int argc = 1;
    static char name[] = "cloudmus-core";
    static char* argv[] = { name, nullptr };

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("cloudmus"));
    QCoreApplication::setApplicationName(QStringLiteral("cloudmus-mobile"));
    Logging::install(QStringLiteral("playback"), QString());
    Logging::mark(QStringLiteral("core thread started"));

    App::MobileCore core({ mediaDir, filesDir + QStringLiteral("/core.ini") });
    coreInstance = &core;
    Remote::Host host(core, filesDir + QStringLiteral("/core.sock"));
    Logging::mark(QStringLiteral("core: remote host listening"));

    auto report = [&core] { reportState(core); };
    auto& now = core.nowPlaying();
    QObject::connect(&now, &ViewModel::NowPlaying::currentIndexChanged, &core, report);
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
    onStateMethod = env->GetStaticMethodID(bridgeClass, "onState", "(IZZJJ)V");
    onAutoplayMethod = env->GetStaticMethodID(bridgeClass, "onAutoplayChanged", "(Z)V");
    onResumeAfterLossMethod = env->GetStaticMethodID(bridgeClass, "onResumeAfterLossChanged", "(I)V");
    onReadyMethod = env->GetStaticMethodID(bridgeClass, "onCoreReady", "()V");
    onStoppedMethod = env->GetStaticMethodID(bridgeClass, "onStopped", "()V");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL Java_dev_cloudmus_mobile_CoreBridge_nativeStart(
    JNIEnv* env, jclass, jstring filesDir, jstring mediaDir)
{
    coreThread = std::thread(runCore, toQString(env, filesDir), toQString(env, mediaDir));
}

extern "C" JNIEXPORT void JNICALL Java_dev_cloudmus_mobile_CoreBridge_nativeCommand(
    JNIEnv*, jclass, jint command, jlong argument)
{
    post([command, argument] {
        if (!coreInstance)
            return;
        auto& now = coreInstance->nowPlaying();
        auto& playlist = coreInstance->activePlaylist();
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
            case PlayIndex:
                playlist.play(int(argument));
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
