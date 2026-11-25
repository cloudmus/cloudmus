#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSslSocket>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <jni.h>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include "Core.h"
#include "Downloads.h"
#include "HostMedia.h"
#include "Host.h"
#include "Jni.h"
#include "Log.h"
#include "Messages.h"
#include "MobileCore.h"
#include "PlaybackController.h"
#include "Throttle.h"
#include "TrackFile.h"

// The core as the playback service runs it: a library loaded by
// CoreBridge.java into the :playback process, with Qt Core and no Qt
// platform plugin (so no Gui). The core lives on a thread of its own with
// its own event loop; Java's commands are posted to it, and its state is
// reported back through CoreBridge.onState().

extern "C" int av_jni_set_java_vm(void* vm, void* logCtx);
extern "C" int av_jni_set_android_app_ctx(void* context, void* logCtx);

namespace {

enum Command {
    Play = 1,
    Pause, // argument: kSystemPause, or 0 for the user's own
    Next,
    Previous,
    Seek,
    Like,
    Dislike,
    SetShuffle, // argument: 0 or 1
    SetRepeat, // argument: a Playback::RepeatMode
    CycleRepeat,
    CancelDownloads,
    // The app or the system started: play on if the music was playing when
    // the last run ended, stay paused if the user had paused it.
    Autoplay
};

// A pause the user didn't ask for: another app took the audio, the output
// went, the service closed. The car turning off comes as one of those, and
// the next start should play on as if the power had just been cut.
constexpr jlong kSystemPause = 1;

jclass bridgeClass = nullptr;
jclass storageClass = nullptr;
jmethodID openContentMethod = nullptr;
jmethodID publishMethod = nullptr;
jmethodID remuxOpusMethod = nullptr;
jmethodID isSavedMethod = nullptr;
jmethodID setWantAllMethod = nullptr;
jmethodID permissionNeededMethod = nullptr;
jmethodID deleteMediaMethod = nullptr;
jmethodID onStateMethod = nullptr;
jmethodID onTrackMethod = nullptr;
jmethodID onQueueMethod = nullptr;
jmethodID onFeedbackMethod = nullptr;
jmethodID onPlayModesMethod = nullptr;
jmethodID onAutoplayMethod = nullptr;
jmethodID onResumeAfterLossMethod = nullptr;
jmethodID defaultResumeAfterLossMethod = nullptr;
jmethodID onReadyMethod = nullptr;
jmethodID onStoppedMethod = nullptr;
jmethodID onBrowseResultMethod = nullptr;
jmethodID onLibraryChangedMethod = nullptr;
jmethodID onDownloadsMethod = nullptr;
jmethodID onSignInMethod = nullptr;

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

QString toQString(JNIEnv* env, jstring text);

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

// The queue around the track, for the system's player to list (the car's
// Media Center): a window of it, the whole of a long playlist being more
// than a binder call takes. Covers as files, which Java hands out as links.
constexpr int kQueueBefore = 10;
constexpr int kQueueAfter = 40;
constexpr int kQueueFields = 6;

struct QueueReport {
    int first = 0;
    int current = -1;
    bool hasPrevious = false;
    bool hasNext = false;
    QStringList flat;
    bool operator==(const QueueReport&) const = default;
};

QueueReport queueReport(App::MobileCore& core)
{
    QueueReport report;
    Playback::PlaybackController& playback = core.core().playback();
    if (!playback.hasCurrentTrack())
        return report;
    const auto& queue = playback.queue();
    const int current = playback.currentIndex();
    report.first = std::max(0, current - kQueueBefore);
    const int end = std::min(int(queue.size()), current + kQueueAfter + 1);
    report.current = current;
    report.hasPrevious = playback.neighborIndex(-1) >= 0;
    // A station has its next track coming even when none is queued yet.
    report.hasNext = playback.neighborIndex(1) >= 0 || playback.isRadio();
    Covers::CoverArtCache& covers = core.core().coverArtCache();
    for (int i = report.first; i < end; ++i) {
        const Playback::QueueEntry& entry = queue.at(i);
        const Track& track = entry.track;
        QStringList artists;
        for (const Artist& artist : track.artists)
            artists.append(artist.name);
        report.flat << entry.sourceId + QLatin1Char('/') + track.id << track.title
                    << artists.join(QStringLiteral(", ")) << (track.album ? track.album->title : QString())
                    << covers.localFile(track.coverUrl.value_or(
                           track.album ? track.album->coverUrl.value_or(QString()) : QString()))
                    << QString::number(track.durationMs);
    }
    return report;
}

void reportQueue(App::MobileCore& core)
{
    static QueueReport last;
    QueueReport report = queueReport(core);
    // Covers arrive one by one, and most of them belong to other lists.
    if (report == last)
        return;
    last = report;
    JNIEnv* env = Android::Jni::env();
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray flat = env->NewObjectArray(jsize(report.flat.size()), stringClass, nullptr);
    for (jsize i = 0; i < jsize(report.flat.size()); ++i) {
        jstring value = toJava(env, report.flat.at(i));
        env->SetObjectArrayElement(flat, i, value);
        env->DeleteLocalRef(value);
    }
    env->CallStaticVoidMethod(bridgeClass, onQueueMethod, jint(report.first), jint(report.current),
        jboolean(report.hasPrevious), jboolean(report.hasNext), flat);
    Android::Jni::checkException(env, "CoreBridge.onQueue");
    env->DeleteLocalRef(flat);
    env->DeleteLocalRef(stringClass);
}

// Like and dislike, for the buttons in the notification.
void reportFeedback(App::MobileCore& core)
{
    JNIEnv* env = Android::Jni::env();
    auto& now = core.nowPlaying();
    env->CallStaticVoidMethod(bridgeClass, onFeedbackMethod, jboolean(now.likeSupported()), jboolean(now.liked()),
        jboolean(now.dislikeSupported()), jboolean(now.disliked()));
    Android::Jni::checkException(env, "CoreBridge.onFeedback");
}

// Shuffle and repeat as in effect, for their buttons; a radio has no list to
// shuffle.
void reportPlayModes(App::MobileCore& core)
{
    JNIEnv* env = Android::Jni::env();
    auto& now = core.nowPlaying();
    env->CallStaticVoidMethod(
        bridgeClass, onPlayModesMethod, jboolean(!now.isRadio()), jboolean(now.shuffle()), jint(now.repeat()));
    Android::Jni::checkException(env, "CoreBridge.onPlayModes");
}

// A media browser's answer, as flat strings: five for each item (see
// CoreBridge.onBrowseResult()).
void reportBrowseResult(jint requestId, const QList<ViewModel::MediaTree::Item>& items)
{
    JNIEnv* env = Android::Jni::env();
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray flat = env->NewObjectArray(jsize(items.size() * 5), stringClass, nullptr);
    jsize at = 0;
    const auto add = [&](const QString& text) {
        jstring value = toJava(env, text);
        env->SetObjectArrayElement(flat, at++, value);
        env->DeleteLocalRef(value);
    };
    for (const ViewModel::MediaTree::Item& item : items) {
        add(item.id);
        add(item.title);
        add(item.subtitle);
        add(item.coverFile);
        add(item.browsable ? QStringLiteral("1") : QString());
    }
    env->CallStaticVoidMethod(bridgeClass, onBrowseResultMethod, requestId, flat);
    Android::Jni::checkException(env, "CoreBridge.onBrowseResult");
    env->DeleteLocalRef(flat);
    env->DeleteLocalRef(stringClass);
}

// What's being saved, for the notification; the same once more isn't sent.
void reportDownloads(App::MobileCore& core)
{
    static ViewModel::DownloadsScreen::Notice last;
    const ViewModel::DownloadsScreen::Notice notice = core.downloadsScreen().notice();
    if (notice == last)
        return;
    last = notice;
    JNIEnv* env = Android::Jni::env();
    const jstring texts[]
        = { toJava(env, notice.title), toJava(env, notice.text), toJava(env, ViewModel::DownloadsScreen::tr("Cancel")),
              toJava(env, ViewModel::DownloadsScreen::tr("Downloads")) };
    env->CallStaticVoidMethod(bridgeClass, onDownloadsMethod, jboolean(notice.shown), jboolean(notice.ongoing),
        jboolean(notice.failed), texts[0], texts[1], jint(notice.permille), texts[2], texts[3]);
    Android::Jni::checkException(env, "CoreBridge.onDownloads");
    for (const jstring text : texts)
        env->DeleteLocalRef(text);
}

int openContent(const QString& uri)
{
    JNIEnv* env = Android::Jni::env();
    const jstring input = toJava(env, uri);
    const int fd = env->CallStaticIntMethod(storageClass, openContentMethod, input);
    env->DeleteLocalRef(input);
    return Android::Jni::checkException(env, "MusicStorage.openContent") ? -1 : fd;
}

bool remuxOpus(const QString& input, const QString& output)
{
    JNIEnv* env = Android::Jni::env();
    const jstring source = toJava(env, input);
    const jstring target = toJava(env, output);
    const bool ready = env->CallStaticBooleanMethod(storageClass, remuxOpusMethod, source, target);
    env->DeleteLocalRef(source);
    env->DeleteLocalRef(target);
    return !Android::Jni::checkException(env, "MusicStorage.remuxOpus") && ready;
}

// MusicStorage.java's index of MediaStore and the app's own folders, for the
// sources with hostMedia (docs/protocol.md §7.10). Lives on the core thread.
class AndroidHostMedia final : public Rpc::HostMedia {
public:
    explicit AndroidHostMedia(QString path)
        : path_(std::move(path))
    {
    }

    QString libraryPath() const override { return QFileInfo::exists(path_) ? path_ : QString(); }
    qint64 revision() const override { return revision_; }

    void want(const QString& sourceId, bool all) override
    {
        if (all)
            wantingAll_.insert(sourceId);
        else
            wantingAll_.remove(sourceId);
        JNIEnv* env = Android::Jni::env();
        env->CallStaticVoidMethod(storageClass, setWantAllMethod, jboolean(!wantingAll_.isEmpty()));
        Android::Jni::checkException(env, "MusicStorage.setWantAll");
        updatePermission();
    }

    bool permissionNeeded() const override { return permissionNeeded_; }

    void remove(const QStringList& uris) override
    {
        JNIEnv* env = Android::Jni::env();
        jclass stringClass = env->FindClass("java/lang/String");
        jobjectArray input = env->NewObjectArray(jsize(uris.size()), stringClass, nullptr);
        for (qsizetype i = 0; i < uris.size(); ++i) {
            jstring uri = toJava(env, uris.at(i));
            env->SetObjectArrayElement(input, jsize(i), uri);
            env->DeleteLocalRef(uri);
        }
        const auto pending
            = static_cast<jobjectArray>(env->CallStaticObjectMethod(storageClass, deleteMediaMethod, input));
        env->DeleteLocalRef(input);
        env->DeleteLocalRef(stringClass);
        if (Android::Jni::checkException(env, "MusicStorage.delete") || pending == nullptr)
            return;
        QStringList needConsent;
        for (jsize i = 0; i < env->GetArrayLength(pending); ++i) {
            auto uri = static_cast<jstring>(env->GetObjectArrayElement(pending, i));
            needConsent.append(toQString(env, uri));
            env->DeleteLocalRef(uri);
        }
        env->DeleteLocalRef(pending);
        if (!needConsent.isEmpty())
            emit removalNeedsConsent(needConsent);
    }

    // MusicStorage wrote a new index.
    void publish()
    {
        ++revision_;
        updatePermission();
        emit changed();
    }

private:
    void updatePermission()
    {
        JNIEnv* env = Android::Jni::env();
        const bool needed = env->CallStaticBooleanMethod(storageClass, permissionNeededMethod);
        if (Android::Jni::checkException(env, "MusicStorage.permissionNeeded") || needed == permissionNeeded_)
            return;
        permissionNeeded_ = needed;
        emit permissionNeededChanged();
    }

    QString path_;
    qint64 revision_ = 1;
    QSet<QString> wantingAll_;
    bool permissionNeeded_ = false;
};

AndroidHostMedia* hostMediaInstance = nullptr; // only touched in the core thread

ViewModel::Downloads::PublishResult publishTrack(const QString& path, const QString& subfolder)
{
    JNIEnv* env = Android::Jni::env();
    const jstring file = toJava(env, path);
    const jstring folder = toJava(env, subfolder);
    const auto reply = static_cast<jstring>(env->CallStaticObjectMethod(storageClass, publishMethod, file, folder));
    env->DeleteLocalRef(file);
    env->DeleteLocalRef(folder);
    if (Android::Jni::checkException(env, "MusicStorage.publish") || reply == nullptr)
        return { { }, { }, QCoreApplication::translate("ViewModel::Downloads", "Couldn't publish the track") };
    const auto result = QJsonDocument::fromJson(toQString(env, reply).toUtf8()).object();
    env->DeleteLocalRef(reply);
    return { result.value(QStringLiteral("directory")).toString(), result.value(QStringLiteral("fallback")).toString(),
        result.value(QStringLiteral("error")).toString() };
}

bool isTrackSaved(const QString& subfolder, const QString& baseName)
{
    JNIEnv* env = Android::Jni::env();
    const QStringList extensions = Library::savedAudioExtensions();
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray array = env->NewObjectArray(jsize(extensions.size()), stringClass, nullptr);
    for (jsize i = 0; i < jsize(extensions.size()); ++i) {
        jstring value = toJava(env, extensions.at(i));
        env->SetObjectArrayElement(array, i, value);
        env->DeleteLocalRef(value);
    }
    const jstring folder = toJava(env, subfolder);
    const jstring name = toJava(env, baseName);
    const bool saved = env->CallStaticBooleanMethod(storageClass, isSavedMethod, folder, name, array);
    const bool failed = Android::Jni::checkException(env, "MusicStorage.isSaved");
    env->DeleteLocalRef(folder);
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(array);
    env->DeleteLocalRef(stringClass);
    return !failed && saved;
}

// What the backends find around them. Their Python and data are the app's,
// and these reach them through this process' environment.
void setUpBackends(
    const QString& filesDir, const QString& cacheDir, const QString& libraryDir, const QString& nativeBackendDir)
{
    // Qt's plugins (TLS, JPEG for the covers) are with the other libraries.
    // Qt's own activity tells it so; this process doesn't have one.
    QCoreApplication::addLibraryPath(libraryDir);
    const auto set = [](const char* name, const QString& value) { qputenv(name, value.toUtf8()); };
    set("HOME", filesDir);
    set("XDG_CONFIG_HOME", filesDir + QStringLiteral("/config"));
    set("XDG_CACHE_HOME", cacheDir);
    set("TMPDIR", cacheDir);
    set("CLOUDMUS_NATIVE_BACKENDS_DIR", nativeBackendDir);
    // Qt's own HTTPS (streams, covers): the OpenSSL that came with Python.
    set("ANDROID_OPENSSL_SUFFIX", QStringLiteral("_python"));
    set("LD_LIBRARY_PATH", libraryDir);
}

void setUpPython(const QString& pythonHome, const QString& libraryDir)
{
    const auto set = [](const char* name, const QString& value) { qputenv(name, value.toUtf8()); };
    set("PYTHONHOME", pythonHome);
    set("PYTHONPATH", pythonHome + QStringLiteral("/site-packages"));
    set("SSL_CERT_FILE", pythonHome + QStringLiteral("/site-packages/certifi/cacert.pem"));
    set("CLOUDMUS_BACKENDS_DIR", pythonHome + QStringLiteral("/backends"));
    set("CLOUDMUS_BACKENDS_PYTHON", libraryDir + QStringLiteral("/libcmpython.so"));
}

void runCore(QString filesDir, QString cacheDir, QString libraryDir, QString nativeBackendDir)
{
    static int argc = 1;
    static char name[] = "cloudmus-core";
    static char* argv[] = { name, nullptr };

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("cloudmus"));
    QCoreApplication::setApplicationName(QStringLiteral("cloudmus-mobile"));
    Logging::install(QStringLiteral("playback"), QString());
    Logging::mark(QStringLiteral("core thread started"));

    setUpBackends(filesDir, cacheDir, libraryDir, nativeBackendDir);
    // A second for the UI to come up on what's saved; a few more before the
    // saved lists are refreshed, behind the music that has started by then.
    AndroidHostMedia hostMedia(filesDir + QStringLiteral("/android-music.json"));
    hostMediaInstance = &hostMedia;
#ifdef CLOUDMUS_STORE_BUILD
    const QString updatesDir; // the store updates the app
#else
    const QString updatesDir = filesDir + QStringLiteral("/updates");
#endif
    App::MobileCore core({ filesDir + QStringLiteral("/core.ini"), 1000, 5000, &hostMedia, updatesDir });
    coreInstance = &core;
    core.core().downloads().setPublisher(QStringLiteral("Music/CloudMus"), publishTrack, remuxOpus, isTrackSaved);
    core.core().playback().setContentOpener(openContent);
    // Without it no stream or cover loads, and nothing else says why.
    qInfo() << "TLS:" << QSslSocket::activeBackend() << QSslSocket::supportsSsl()
            << QSslSocket::sslLibraryVersionString();
    qInfo() << "image formats:" << QImageWriter::supportedImageFormats();
    Remote::Host host(core, filesDir + QStringLiteral("/core.sock"));
    Logging::mark(QStringLiteral("core: remote host listening"));

    auto report = [&core] { reportState(core); };
    auto& now = core.nowPlaying();
    // Before the track: Java finds the new track in the queue it has, rather
    // than a moment of the old queue without it.
    auto queue = [&core] { reportQueue(core); };
    QObject::connect(&now, &ViewModel::NowPlaying::trackChanged, &core, queue);
    // The rest in bursts (a radio's tracks, a list's covers): once it settles.
    QTimer queueLater;
    queueLater.setSingleShot(true);
    queueLater.setInterval(200);
    QObject::connect(&queueLater, &QTimer::timeout, &core, queue);
    const auto queueSoon = [&queueLater] { queueLater.start(); };
    QObject::connect(&core.core().playback(), &Playback::PlaybackController::queueChanged, &core, queueSoon);
    QObject::connect(&now, &ViewModel::NowPlaying::playModesChanged, &core, queueSoon);
    QObject::connect(&core.core().coverArtCache(), &Covers::CoverArtCache::pixmapReady, &core, queueSoon);
    auto track = [&core] { reportTrack(core); };
    QObject::connect(&now, &ViewModel::NowPlaying::trackChanged, &core, track);
    auto feedback = [&core] { reportFeedback(core); };
    QObject::connect(&now, &ViewModel::NowPlaying::trackChanged, &core, feedback);
    QObject::connect(&now, &ViewModel::NowPlaying::feedbackChanged, &core, feedback);
    QObject::connect(&now, &ViewModel::NowPlaying::coverUrlChanged, &core, track);
    QObject::connect(&now, &ViewModel::NowPlaying::playModesChanged, &core, [&core] { reportPlayModes(core); });
    QObject::connect(&now, &ViewModel::NowPlaying::durationMsChanged, &core, report);
    QObject::connect(&now, &ViewModel::NowPlaying::playingChanged, &core, report);
    QObject::connect(&now, &ViewModel::NowPlaying::loadingChanged, &core, report);
    QObject::connect(&now, &ViewModel::NowPlaying::positionMsChanged, &core, report);
    QObject::connect(&now, &ViewModel::NowPlaying::stopped, &core, [] {
        JNIEnv* env = Android::Jni::env();
        env->CallStaticVoidMethod(bridgeClass, onStoppedMethod);
        Android::Jni::checkException(env, "CoreBridge.onStopped");
    });
    QObject::connect(&core.mediaTree(), &ViewModel::MediaTree::changed, &core, [] {
        JNIEnv* env = Android::Jni::env();
        env->CallStaticVoidMethod(bridgeClass, onLibraryChangedMethod);
        Android::Jni::checkException(env, "CoreBridge.onLibraryChanged");
    });
    // A notification's progress at most every second: Android drops updates
    // that come faster.
    auto* downloadsLater = new Timing::Throttle(1000, [&core] { reportDownloads(core); }, &core);
    QObject::connect(&core.core().downloads(), &ViewModel::Downloads::changed, downloadsLater,
        [downloadsLater] { downloadsLater->trigger(); });
    // A code being confirmed in a browser: the process is kept while it is
    // (SignInService), or a phone short of memory unloads it meanwhile.
    auto signIn = [&core] {
        const App::MobileCore::SignInNotice notice = core.signInNotice();
        JNIEnv* env = Android::Jni::env();
        const jstring texts[]
            = { toJava(env, notice.title), toJava(env, notice.text), toJava(env, notice.channelName) };
        env->CallStaticVoidMethod(bridgeClass, onSignInMethod, texts[0], texts[1], texts[2]);
        Android::Jni::checkException(env, "CoreBridge.onSignIn");
        for (const jstring text : texts)
            env->DeleteLocalRef(text);
    };
    QObject::connect(&core, &App::MobileCore::signInNoticeChanged, &core, signIn);
    core.settings().onAutoplayChanged = [](bool enabled) {
        JNIEnv* env = Android::Jni::env();
        env->CallStaticVoidMethod(bridgeClass, onAutoplayMethod, jboolean(enabled));
        Android::Jni::checkException(env, "CoreBridge.onAutoplayChanged");
    };
    {
        JNIEnv* env = Android::Jni::env();
        const jint seconds = env->CallStaticIntMethod(bridgeClass, defaultResumeAfterLossMethod);
        if (!Android::Jni::checkException(env, "CoreBridge.defaultResumeAfterLossSec"))
            core.settings().setResumeAfterLossDefault(seconds);
    }
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
    reportQueue(core);
    reportTrack(core);
    reportFeedback(core);
    reportPlayModes(core);
    reportState(core);
    JNIEnv* env = Android::Jni::env();
    env->CallStaticVoidMethod(bridgeClass, onReadyMethod);
    Android::Jni::checkException(env, "CoreBridge.onCoreReady");

    app.exec();

    std::lock_guard lock(mutex);
    receiver = nullptr;
    coreInstance = nullptr;
    hostMediaInstance = nullptr;
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
    jclass local = env->FindClass("app/cloudmus/mobile/CoreBridge");
    bridgeClass = static_cast<jclass>(env->NewGlobalRef(local));
    onStateMethod = env->GetStaticMethodID(bridgeClass, "onState", "(ZZJJ)V");
    onFeedbackMethod = env->GetStaticMethodID(bridgeClass, "onFeedback", "(ZZZZ)V");
    onPlayModesMethod = env->GetStaticMethodID(bridgeClass, "onPlayModes", "(ZZI)V");
    onTrackMethod = env->GetStaticMethodID(bridgeClass, "onTrack",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JLjava/lang/String;)V");
    onQueueMethod = env->GetStaticMethodID(bridgeClass, "onQueue", "(IIZZ[Ljava/lang/String;)V");
    onAutoplayMethod = env->GetStaticMethodID(bridgeClass, "onAutoplayChanged", "(Z)V");
    onResumeAfterLossMethod = env->GetStaticMethodID(bridgeClass, "onResumeAfterLossChanged", "(I)V");
    defaultResumeAfterLossMethod = env->GetStaticMethodID(bridgeClass, "defaultResumeAfterLossSec", "()I");
    jclass storage = env->FindClass("app/cloudmus/mobile/MusicStorage");
    if (Android::Jni::checkException(env, "FindClass MusicStorage") || !storage)
        return JNI_ERR;
    storageClass = static_cast<jclass>(env->NewGlobalRef(storage));
    env->DeleteLocalRef(storage);
    openContentMethod = env->GetStaticMethodID(storageClass, "openContent", "(Ljava/lang/String;)I");
    publishMethod
        = env->GetStaticMethodID(storageClass, "publish", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    remuxOpusMethod = env->GetStaticMethodID(storageClass, "remuxOpus", "(Ljava/lang/String;Ljava/lang/String;)Z");
    isSavedMethod
        = env->GetStaticMethodID(storageClass, "isSaved", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)Z");
    setWantAllMethod = env->GetStaticMethodID(storageClass, "setWantAll", "(Z)V");
    permissionNeededMethod = env->GetStaticMethodID(storageClass, "permissionNeeded", "()Z");
    deleteMediaMethod = env->GetStaticMethodID(storageClass, "delete", "([Ljava/lang/String;)[Ljava/lang/String;");
    if (Android::Jni::checkException(env, "MusicStorage methods"))
        return JNI_ERR;
    onReadyMethod = env->GetStaticMethodID(bridgeClass, "onCoreReady", "()V");
    onStoppedMethod = env->GetStaticMethodID(bridgeClass, "onStopped", "()V");
    onBrowseResultMethod = env->GetStaticMethodID(bridgeClass, "onBrowseResult", "(I[Ljava/lang/String;)V");
    onLibraryChangedMethod = env->GetStaticMethodID(bridgeClass, "onLibraryChanged", "()V");
    onDownloadsMethod = env->GetStaticMethodID(
        bridgeClass, "onDownloads", "(ZZZLjava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;)V");
    onSignInMethod = env->GetStaticMethodID(
        bridgeClass, "onSignIn", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_CoreBridge_nativeStart(
    JNIEnv* env, jclass, jobject application, jstring filesDir, jstring cacheDir, jstring libraryDir,
    jstring nativeBackendDir)
{
    // FFmpeg's content protocol uses this reference for the lifetime of the process.
    static const bool contextRegistered = [env, application] {
        jobject context = env->NewGlobalRef(application);
        if (context && av_jni_set_android_app_ctx(context, nullptr) == 0)
            return true;
        env->DeleteGlobalRef(context);
        return false;
    }();
    if (!contextRegistered)
        qWarning() << "could not initialize the Android context for FFmpeg";
    coreThread = std::thread(runCore, toQString(env, filesDir), toQString(env, cacheDir), toQString(env, libraryDir),
        toQString(env, nativeBackendDir));
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_CoreBridge_nativePythonReady(
    JNIEnv* env, jclass, jstring pythonHome, jstring libraryDir)
{
    post([home = toQString(env, pythonHome), libraries = toQString(env, libraryDir)] {
        if (!coreInstance)
            return;
        setUpPython(home, libraries);
        coreInstance->core().sourceManager().discoverNewSources();
    });
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_CoreBridge_nativeMusicChanged(JNIEnv*, jclass)
{
    // The sources with hostMedia get it, and are listed anew.
    post([] {
        if (hostMediaInstance)
            hostMediaInstance->publish();
    });
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_CoreBridge_nativeOpenAudio(
    JNIEnv* env, jclass, jstring uri, jstring title, jstring artist, jstring album, jstring cover, jlong duration,
    jboolean persistent)
{
    const QString url = toQString(env, uri);
    const QString trackTitle = toQString(env, title);
    const QString artistName = toQString(env, artist);
    const QString albumTitle = toQString(env, album);
    const QString coverUrl = toQString(env, cover);
    post([url, trackTitle, artistName, albumTitle, coverUrl, duration, persistent] {
        if (!coreInstance)
            return;
        if (url.isEmpty()) {
            coreInstance->core().activePlaylist().reportLocalInputError();
            return;
        }
        Track track;
        track.id = QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex());
        track.title = trackTitle;
        if (!artistName.isEmpty())
            track.artists.append(Artist { artistName, artistName });
        if (!albumTitle.isEmpty())
            track.album = Album { albumTitle, albumTitle };
        if (!coverUrl.isEmpty())
            track.coverUrl = coverUrl;
        track.durationMs = int(std::clamp<jlong>(duration, 0, std::numeric_limits<int>::max()));
        coreInstance->core().activePlaylist().playLocalInput(track, url, persistent);
    });
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_CoreBridge_nativeCommand(
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
            case Pause: {
                Config::Settings& settings = coreInstance->core().settings();
                const bool wasPlaying = settings.wasPlaying();
                now.pause(); // clears wasPlaying (ActivePlaylist, on halted())
                if (argument == kSystemPause && wasPlaying)
                    settings.setWasPlaying(true);
                break;
            }
            case Next:
                now.next();
                break;
            case Previous:
                now.previous();
                break;
            case Seek:
                now.seek(argument);
                break;
            case Like:
                now.setLiked(!now.liked());
                break;
            case Dislike:
                now.setDisliked(!now.disliked());
                break;
            case SetShuffle:
                now.setShuffle(argument != 0);
                break;
            case SetRepeat:
                if (argument >= int(Playback::RepeatMode::Off) && argument <= int(Playback::RepeatMode::One))
                    now.setRepeatMode(Playback::RepeatMode(argument));
                break;
            case CycleRepeat:
                now.cycleRepeat();
                break;
            case CancelDownloads:
                coreInstance->core().downloads().cancelAll();
                break;
            case Autoplay:
                if (coreInstance->core().settings().wasPlaying())
                    now.play();
                else
                    qInfo() << "autoplay: paused when last left, staying paused";
                break;
            default:
                break;
        }
    });
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_CoreBridge_nativeBrowse(
    JNIEnv* env, jclass, jint requestId, jstring id, jboolean itemItself)
{
    post([requestId, id = toQString(env, id), itemItself] {
        QList<ViewModel::MediaTree::Item> items;
        if (coreInstance && itemItself) {
            const ViewModel::MediaTree::Item item = coreInstance->mediaTree().item(id);
            if (!item.id.isEmpty())
                items.append(item);
        } else if (coreInstance) {
            items = coreInstance->mediaTree().children(id);
        }
        reportBrowseResult(requestId, items);
    });
}

// A car's search: answered as a folder would be (CoreBridge.onBrowseResult()).
extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_CoreBridge_nativeSearch(
    JNIEnv* env, jclass, jint requestId, jstring query)
{
    post([requestId, query = toQString(env, query)] {
        reportBrowseResult(requestId, coreInstance ? coreInstance->mediaTree().search(query)
                                                   : QList<ViewModel::MediaTree::Item>());
    });
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_CoreBridge_nativePlayMedia(JNIEnv* env, jclass, jstring id)
{
    post([id = toQString(env, id)] {
        if (coreInstance) {
            qInfo() << "system asks to play" << id;
            coreInstance->mediaTree().play(id);
        }
    });
}

// A track picked in the system's list of the queue: by its place, if the
// queue hasn't moved since, else wherever it is now.
extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_CoreBridge_nativePlayQueueItem(
    JNIEnv* env, jclass, jint index, jstring id)
{
    post([index, id = toQString(env, id)] {
        if (!coreInstance)
            return;
        const auto& queue = coreInstance->core().playback().queue();
        const auto matches = [&](int at) {
            const Playback::QueueEntry& entry = queue.at(at);
            return entry.sourceId + QLatin1Char('/') + entry.track.id == id;
        };
        int at = index >= 0 && index < queue.size() && matches(index) ? index : -1;
        for (int i = 0; at < 0 && i < queue.size(); ++i) {
            if (matches(i))
                at = i;
        }
        qInfo() << "system asks to play the queue's" << at << id;
        if (at >= 0)
            coreInstance->activePlaylist().playRow(at);
    });
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_CoreBridge_nativeStop(JNIEnv*, jclass)
{
    post([] { QCoreApplication::quit(); });
}
