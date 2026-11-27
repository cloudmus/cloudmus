#include "Platform.h"

#include <QAccessible>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcess>
#include <QQuickWindow>
#include <QScreen>
#include <QStringList>

#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include <atomic>

#include <cstdlib>
#include <memory>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

#include <algorithm>

#include "Log.h"
#include "People.h"

namespace Ui {

namespace {

Q_LOGGING_CATEGORY(lcPlatform, "cloudmus.ui.platform")

#ifdef Q_OS_ANDROID
// The one there is, for what Android calls with nothing to go by.
std::atomic<Platform*> instance { nullptr };

QString bridgeString(const char* method)
{
    return QJniObject::callStaticMethod<jstring>(
        "app/cloudmus/mobile/UiBridge", method, QNativeInterface::QAndroidApplication::context())
        .toString();
}
#endif

} // namespace

Platform::Platform(QString logDirectory, QObject* parent)
    : QObject(parent)
    , logDirectory_(std::move(logDirectory))
{
#ifdef Q_OS_ANDROID
    if (!bridgeString("takePythonProbeRequest").isEmpty())
        QTimer::singleShot(0, this, &Platform::runPythonProbe);
    instance = this;
    readInsets();
    readVolume();
    // After the window has loaded, to see it.
    QTimer::singleShot(0, this, &Platform::takeOpenRequests);
#endif
}

// The export can't be stopped halfway; quitting waits for it rather than
// destroying a running thread.
Platform::~Platform()
{
#ifdef Q_OS_ANDROID
    instance = nullptr;
#endif
    if (exportThread_)
        exportThread_->wait();
}

QString Platform::publicLogDirectory() const
{
#ifdef Q_OS_ANDROID
    return bridgeString("publicLogDirectory");
#else
    return { };
#endif
}

QString Platform::deviceInfo() const
{
    QString info;
#ifdef Q_OS_ANDROID
    info = bridgeString("deviceInfo") + QLatin1Char('\n');
#else
    info = QSysInfo::prettyProductName() + QLatin1Char('\n');
#endif
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QSize px = screen->size() * screen->devicePixelRatio();
        info += QStringLiteral("Screen %1×%2 px, %3×%4 dp, ratio %5, %6 dpi")
                    .arg(px.width())
                    .arg(px.height())
                    .arg(screen->size().width())
                    .arg(screen->size().height())
                    .arg(screen->devicePixelRatio())
                    .arg(qRound(screen->physicalDotsPerInch()));
    }
    return info;
}

bool Platform::android() const
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

bool Platform::hasSignInBrowser() const
{
#ifdef Q_OS_ANDROID
    return !bridgeString("hasWebView").isEmpty();
#else
    return false;
#endif
}

bool Platform::signInBrowserTakesProxy() const
{
#ifdef Q_OS_ANDROID
    return !bridgeString("webViewTakesProxy").isEmpty();
#else
    return false;
#endif
}

bool Platform::signInBrowserOutdated() const
{
#ifdef Q_OS_ANDROID
    return !bridgeString("webViewOutdated").isEmpty();
#else
    return false;
#endif
}

bool Platform::canSignInByPhone() const { return android(); }

QString Platform::startPhoneSignIn(const QString& request, const QString& method, const QString& title) const
{
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticObjectMethod("app/cloudmus/mobile/PhoneSignInServer", "start",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        QNativeInterface::QAndroidApplication::context().object(), QJniObject::fromString(request).object<jstring>(),
        QJniObject::fromString(method).object<jstring>(), QJniObject::fromString(title).object<jstring>())
        .toString();
#else
    Q_UNUSED(request)
    Q_UNUSED(method)
    Q_UNUSED(title)
    return { };
#endif
}

void Platform::stopPhoneSignIn() const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/PhoneSignInServer", "stop");
#endif
}

void Platform::signInWithBrowser(const QString& request, const QString& method, const QString& title) const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UiBridge", "signInWithBrowser",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        QNativeInterface::QAndroidApplication::context().object(), QJniObject::fromString(request).object<jstring>(),
        QJniObject::fromString(method).object<jstring>(), QJniObject::fromString(title).object<jstring>());
#else
    Q_UNUSED(request)
    Q_UNUSED(method)
    Q_UNUSED(title)
#endif
}

QString Platform::version() const { return QStringLiteral(CLOUDMUS_MOBILE_VERSION); }

QString Platform::licenseText() const
{
    QFile file(QStringLiteral(":/about/LICENSE"));
    if (!file.open(QIODevice::ReadOnly))
        return { };
    // LICENSE is hard-wrapped at 80 columns: each paragraph's lines rejoined,
    // it reflows to the screen instead of reading ragged (as on the desktop).
    QStringList paragraphs = QString::fromUtf8(file.readAll()).split(QStringLiteral("\n\n"));
    for (QString& paragraph : paragraphs)
        paragraph = paragraph.simplified();
    paragraphs.removeAll(QString());
    return paragraphs.join(QLatin1Char('\n'));
}

namespace {
QVariantList toVariants(const QList<About::Person>& people)
{
    QVariantList list;
    for (const About::Person& person : people) {
        list.append(QVariantMap { { QStringLiteral("name"), person.name }, { QStringLiteral("email"), person.email },
            { QStringLiteral("url"), person.url }, { QStringLiteral("shownUrl"), About::shownUrl(person.url) } });
    }
    return list;
}
} // namespace

QVariantList Platform::authors() const { return toVariants(About::authors()); }

QVariantList Platform::contributors() const { return toVariants(About::contributors()); }

void Platform::exitNow(int code)
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/CmLog", "cleanExit");
    std::_Exit(code);
#else
    Q_UNUSED(code)
#endif
}

void Platform::limitAccessibility()
{
#ifdef Q_OS_ANDROID
    // With any accessibility service on (KDE Connect has one), Qt tells Android
    // of every item that moves, and each such call holds Android's main thread
    // until the Qt thread answers. An animation or a turn of the screen moves
    // everything: the app stopped responding, for as long as a minute. A service
    // that only watches the screen doesn't need any of it; a screen reader does.
    if (bridgeString("screenReaderOn").isEmpty()) {
        qCInfo(lcPlatform) << "no screen reader: Qt's accessibility updates are off";
        QAccessible::installUpdateHandler([](QAccessibleEvent*) { });
    }
#endif
}

void Platform::copyText(const QString& text) const { QGuiApplication::clipboard()->setText(text); }

void Platform::runPythonProbe()
{
#ifdef Q_OS_ANDROID
    qCInfo(lcPlatform) << "python probe: starting";
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, clock] {
        const QStringList paths = watcher->result().split(QLatin1Char('\n'));
        watcher->deleteLater();
        if (paths.size() != 2) {
            emit pythonProbeFinished(tr("Python isn't in this APK"));
            return;
        }
        qCInfo(lcPlatform) << "python probe: unpacked after" << clock->elapsed() << "ms";
        const QString home = paths[0];
        const QString libs = paths[1];
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("LD_LIBRARY_PATH"), libs);
        environment.insert(QStringLiteral("PYTHONHOME"), home);
        environment.insert(QStringLiteral("PYTHONPATH"), home + QStringLiteral("/site-packages"));
        environment.insert(QStringLiteral("SSL_CERT_FILE"), home + QStringLiteral("/site-packages/certifi/cacert.pem"));
        environment.insert(QStringLiteral("HOME"), home + QStringLiteral("/.."));
        auto* process = new QProcess(this);
        process->setProcessEnvironment(environment);
        process->setProcessChannelMode(QProcess::MergedChannels);
        connect(process, &QProcess::readyRead, this, [process, clock] {
            while (process->canReadLine()) {
                qCInfo(lcPlatform) << "python probe:" << clock->elapsed()
                                   << "ms:" << QString::fromUtf8(process->readLine()).trimmed();
            }
        });
        connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
            qCInfo(lcPlatform) << "python probe: rest:" << process->readAll();
            qCInfo(lcPlatform) << "python probe: exit" << code << status;
            process->deleteLater();
            emit pythonProbeFinished(code == 0 && status == QProcess::NormalExit
                    ? tr("Python works: the details are in the log")
                    : tr("Python failed: the details are in the log"));
        });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            qCWarning(lcPlatform) << "python probe: can't start:" << process->errorString();
            process->deleteLater();
            emit pythonProbeFinished(tr("Python failed: the details are in the log"));
        });
        process->start(libs + QStringLiteral("/libcmpython.so"), { home + QStringLiteral("/probe.py") });
    });
    watcher->setFuture(QtConcurrent::run([] { return bridgeString("preparePython"); }));
#else
    emit pythonProbeFinished(tr("Only on Android"));
#endif
}

QString Platform::exportLogs() const
{
#ifdef Q_OS_ANDROID
    return bridgeString("exportLogs");
#else
    return tr("The log is in %1").arg(logDirectory_);
#endif
}

QString Platform::logSystemProbe() const
{
#ifdef Q_OS_ANDROID
    return bridgeString("logSystemProbe");
#else
    return tr("Android only");
#endif
}

QString Platform::probeMusicStorage() const
{
#ifdef Q_OS_ANDROID
    bridgeString("probeMusicStorage");
#endif
    return tr("Written to the log");
}

void Platform::requestMediaDelete(const QStringList& uris)
{
#ifdef Q_OS_ANDROID
    QJniEnvironment env;
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray items = env->NewObjectArray(jsize(uris.size()), stringClass, nullptr);
    for (qsizetype i = 0; i < uris.size(); ++i)
        env->SetObjectArrayElement(items, jsize(i), QJniObject::fromString(uris.at(i)).object<jstring>());
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UiBridge", "requestMediaDelete",
        "(Landroid/content/Context;[Ljava/lang/String;)V", QNativeInterface::QAndroidApplication::context(), items);
    env->DeleteLocalRef(items);
    env->DeleteLocalRef(stringClass);
#else
    Q_UNUSED(uris);
#endif
}

void Platform::requestMusicPermission()
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(
        "app/cloudmus/mobile/UiBridge", "requestMusicPermission", QNativeInterface::QAndroidApplication::context());
#endif
}

void Platform::pickPath(bool directory) const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UiBridge", "pickPath",
        "(Landroid/content/Context;Ljava/lang/String;)V", QNativeInterface::QAndroidApplication::context().object(),
        QJniObject::fromString(directory ? QStringLiteral("yes") : QString()).object<jstring>());
#else
    Q_UNUSED(directory)
#endif
}

void Platform::requestNotificationPermission() const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UiBridge", "requestNotificationPermission",
        QNativeInterface::QAndroidApplication::context());
#endif
}

void Platform::grantMusicStorage() const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(
        "app/cloudmus/mobile/UiBridge", "grantMusicStorage", QNativeInterface::QAndroidApplication::context());
#endif
}

void Platform::exportSystemApps()
{
#ifdef Q_OS_ANDROID
    if (exportThread_)
        return;
    // QJniObject attaches the worker thread to the VM, and finds the app's
    // classes from it too.
    auto result = std::make_shared<QString>();
    exportThread_ = QThread::create([result] { *result = bridgeString("exportSystemApps"); });
    if (!exportPoll_) {
        exportPoll_ = new QTimer(this);
        exportPoll_->setInterval(250);
        connect(exportPoll_, &QTimer::timeout, this, &Platform::pollExportProgress);
    }
    connect(exportThread_, &QThread::finished, this, [this, result] {
        exportPoll_->stop();
        exportThread_->deleteLater();
        exportThread_ = nullptr;
        emit exportProgressChanged();
        emit systemAppsExported(*result);
    });
    exportFraction_ = -1;
    exportStatus_ = tr("Listing packages…");
    exportThread_->start();
    exportPoll_->start();
    emit exportProgressChanged();
#else
    emit systemAppsExported(tr("Android only"));
#endif
}

void Platform::pollExportProgress()
{
#ifdef Q_OS_ANDROID
    // "<done bytes>\t<total bytes>\t<package>", SystemProbe.exportProgress().
    const QStringList parts = bridgeString("exportProgress").split(QLatin1Char('\t'));
    if (parts.size() < 3)
        return;
    const double done = parts[0].toDouble();
    const double total = parts[1].toDouble();
    if (total <= 0 || parts[2].isEmpty())
        return;
    constexpr double mb = 1024.0 * 1024.0;
    exportFraction_ = qBound(0.0, done / total, 1.0);
    exportStatus_ = tr("%1 — %2 of %3 MB").arg(parts[2]).arg(qRound(done / mb)).arg(qRound(total / mb));
    emit exportProgressChanged();
#endif
}

void Platform::markFirstFrame(QObject* window) const
{
    Logging::mark(QStringLiteral("first frame"));
    auto* quickWindow = qobject_cast<QQuickWindow*>(window);
    if (!quickWindow)
        return;
    const QScreen* screen = quickWindow->screen();
    qCInfo(lcPlatform).nospace() << "drawing: " << (screen ? screen->refreshRate() : 0) << " Hz screen, graphics API "
                                 << quickWindow->rendererInterface()->graphicsApi() << ", render loop "
                                 << (qEnvironmentVariableIsSet("QSG_RENDER_LOOP") ? qgetenv("QSG_RENDER_LOOP")
                                                                                  : QByteArray("default"))
                                 << ", touch compression "
                                 << (qEnvironmentVariableIsSet("QML_NO_TOUCH_COMPRESSION") ? "off" : "on")
                                 << ", device pixel ratio " << quickWindow->devicePixelRatio();
}

QRectF Platform::splashIcon() const
{
#ifdef Q_OS_ANDROID
    const QStringList parts = bridgeString("splashIcon").split(QLatin1Char(' '));
    if (parts.size() == 4)
        return QRectF(parts.at(0).toDouble(), parts.at(1).toDouble(), parts.at(2).toDouble(), parts.at(3).toDouble());
#endif
    return { };
}

void Platform::hideSplash() const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(
        "app/cloudmus/mobile/UiBridge", "hideSplash", QNativeInterface::QAndroidApplication::context());
#endif
}

void Platform::setAppearance(const QString& scheme, bool dark, const QString& barColor, const QString& backColor) const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UiBridge", "setAppearance",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        QNativeInterface::QAndroidApplication::context().object(), QJniObject::fromString(scheme).object<jstring>(),
        QJniObject::fromString(dark ? QStringLiteral("yes") : QString()).object<jstring>(),
        QJniObject::fromString(barColor).object<jstring>(), QJniObject::fromString(backColor).object<jstring>());
#else
    Q_UNUSED(scheme)
    Q_UNUSED(dark)
    Q_UNUSED(barColor)
    Q_UNUSED(backColor)
#endif
}

void Platform::setStatusBarOverAura(bool over) const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UiBridge", "setStatusBarOverAura",
        "(Landroid/content/Context;Ljava/lang/String;)V", QNativeInterface::QAndroidApplication::context().object(),
        QJniObject::fromString(over ? QStringLiteral("yes") : QString()).object<jstring>());
#else
    Q_UNUSED(over)
#endif
}

void Platform::setScreenAwake(bool on) const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UiBridge", "setScreenAwake",
        "(Landroid/content/Context;Ljava/lang/String;)V", QNativeInterface::QAndroidApplication::context().object(),
        QJniObject::fromString(on ? QStringLiteral("yes") : QString()).object<jstring>());
#else
    Q_UNUSED(on)
#endif
}

void Platform::readVolume()
{
#ifdef Q_OS_ANDROID
    const QStringList parts = bridgeString("volume").split(QLatin1Char(' '));
    if (parts.size() != 2)
        return;
    const int volume = parts.at(0).toInt();
    const int max = parts.at(1).toInt();
    if (volume == volume_ && max == volumeMax_)
        return;
    volume_ = volume;
    volumeMax_ = max;
    emit systemVolumeChanged();
#endif
}

void Platform::setSystemVolume(int index)
{
#ifdef Q_OS_ANDROID
    index = std::clamp(index, 0, volumeMax_);
    if (volumeMax_ <= 0 || index == volume_)
        return;
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UiBridge", "setVolume", "(Landroid/content/Context;I)V",
        QNativeInterface::QAndroidApplication::context().object(), jint(index));
    // What the system took, not what was asked for.
    readVolume();
#else
    Q_UNUSED(index)
#endif
}

void Platform::readInsets()
{
#ifdef Q_OS_ANDROID
    const QStringList parts = bridgeString("windowInsets").split(QLatin1Char(' '));
    if (parts.size() != 5)
        return;
    insetTop_ = parts.at(0).toInt();
    insetLeft_ = parts.at(1).toInt();
    insetRight_ = parts.at(2).toInt();
    insetBottom_ = parts.at(3).toInt();
    insetKeyboard_ = parts.at(4).toInt();
    emit insetsChanged();
#endif
}

void Platform::takeOpenRequests()
{
#ifdef Q_OS_ANDROID
    if (!bridgeString("takeAudioOpened").isEmpty())
        emit audioOpened();
    if (!bridgeString("takeDownloadsOpened").isEmpty())
        emit downloadsOpened();
#endif
}

QString Platform::updateChannel() const
{
#if defined(Q_OS_ANDROID) && defined(CLOUDMUS_STORE_BUILD)
    static const QString store = QJniObject::callStaticMethod<jstring>("app/cloudmus/mobile/UpdateInstaller",
        "installer", "(Landroid/content/Context;)Ljava/lang/String;", QNativeInterface::QAndroidApplication::context())
                                     .toString();
    return store.isEmpty() ? QStringLiteral("page") : store;
#elif defined(Q_OS_ANDROID)
    return QStringLiteral("app");
#else
    return QStringLiteral("page");
#endif
}

bool Platform::canInstallPackages() const
{
#ifdef Q_OS_ANDROID
    return !QJniObject::callStaticMethod<jstring>("app/cloudmus/mobile/UpdateInstaller", "canInstall",
        "(Landroid/content/Context;)Ljava/lang/String;", QNativeInterface::QAndroidApplication::context())
                .toString()
                .isEmpty();
#else
    return false;
#endif
}

void Platform::requestInstallPermission() const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UpdateInstaller", "requestPermission",
        "(Landroid/content/Context;)V", QNativeInterface::QAndroidApplication::context());
#endif
}

void Platform::installUpdate(const QString& path, bool playing, const QString& noticeChannel,
    const QString& noticeTitle, const QString& noticeText) const
{
#ifdef Q_OS_ANDROID
    qCInfo(lcPlatform).noquote() << "installing the update" << path;
    QJniEnvironment env;
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray notice = env->NewObjectArray(3, stringClass, nullptr);
    const QStringList texts { noticeChannel, noticeTitle, noticeText };
    for (int i = 0; i < texts.size(); ++i)
        env->SetObjectArrayElement(notice, jsize(i), QJniObject::fromString(texts.at(i)).object<jstring>());
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UpdateInstaller", "install",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)V",
        QNativeInterface::QAndroidApplication::context(), QJniObject::fromString(path).object<jstring>(),
        QJniObject::fromString(playing ? QStringLiteral("yes") : QString()).object<jstring>(), notice);
    env->DeleteLocalRef(notice);
    env->DeleteLocalRef(stringClass);
#else
    Q_UNUSED(path);
    Q_UNUSED(playing);
    Q_UNUSED(noticeChannel);
    Q_UNUSED(noticeTitle);
    Q_UNUSED(noticeText);
#endif
}

void Platform::checkStoreUpdate(bool manual) const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/StoreUpdates", "check",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
        QNativeInterface::QAndroidApplication::context(), QJniObject::fromString(updateChannel()).object<jstring>(),
        QJniObject::fromString(manual ? QStringLiteral("yes") : QString()).object<jstring>());
#else
    Q_UNUSED(manual);
#endif
}

void Platform::startStoreUpdate()
{
    storeUpdateProgress_ = -1;
    emit storeUpdateChanged();
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/StoreUpdates", "start",
        "(Landroid/content/Context;Ljava/lang/String;)V", QNativeInterface::QAndroidApplication::context(),
        QJniObject::fromString(updateChannel()).object<jstring>());
#endif
}

void Platform::startUpdateServer()
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UpdateServer", "start", "(Landroid/content/Context;)V",
        QNativeInterface::QAndroidApplication::context());
#endif
    readUpdateServer();
}

void Platform::stopUpdateServer()
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UpdateServer", "stop", "(Landroid/content/Context;)V",
        QNativeInterface::QAndroidApplication::context());
#endif
    readUpdateServer();
}

void Platform::chooseServedApk() const
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("app/cloudmus/mobile/UpdateServer", "choose", "(Landroid/content/Context;)V",
        QNativeInterface::QAndroidApplication::context());
#endif
}

void Platform::readUpdateServer()
{
#ifdef Q_OS_ANDROID
    const QString status = QJniObject::callStaticMethod<jstring>("app/cloudmus/mobile/UpdateServer", "status",
        "(Landroid/content/Context;)Ljava/lang/String;", QNativeInterface::QAndroidApplication::context())
                               .toString();
    if (status == updateServerStatus_)
        return;
    updateServerStatus_ = status;
    emit updateServerChanged();
#endif
}

void Platform::takeInstallOutcome(bool cancelled, int installerStatus, const QString& message)
{
    if (cancelled) {
        emit updateInstallFinished(true, QString());
        return;
    }
    // PackageInstaller's STATUS_FAILURE_*.
    QString error;
    switch (installerStatus) {
        case 5: // CONFLICT
        case 7: // INCOMPATIBLE
            // Most often a key other than the installed app's.
            error = tr("Android refused the new version: it doesn't match the installed one (signed with another "
                       "key?). Install it from the release page.");
            break;
        case 6: // STORAGE
            error = tr("Not enough space on the device to install the update.");
            break;
        case 2: // BLOCKED
            error = tr("Installing was blocked on this device.");
            break;
        case 4: // INVALID
            error = tr("The downloaded file isn't a valid app package.");
            break;
        default:
            error = message.isEmpty() ? tr("Android couldn't install the update.") : message;
    }
    if (!message.isEmpty() && !error.contains(message))
        error += QStringLiteral(" (%1)").arg(message);
    emit updateInstallFinished(false, error);
}

void Platform::takeStoreState(int state, qint64 done, qint64 total, const QString& error)
{
    // StoreUpdates.STATE_*.
    switch (state) {
        case 0:
            storeUpdateProgress_ = total > 0 ? double(done) / double(total) : -1;
            emit storeUpdateChanged();
            break;
        case 1:
            storeUpdateProgress_ = 1;
            emit storeUpdateChanged();
            break;
        case 2:
            emit storeUpdateEnded(false, error.isEmpty() ? tr("The store couldn't update the app.") : error);
            break;
        default:
            emit storeUpdateEnded(true, QString());
    }
}

void Platform::startService()
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(
        "app/cloudmus/mobile/UiBridge", "startService", QNativeInterface::QAndroidApplication::context());
#endif
}

} // namespace Ui

#ifdef Q_OS_ANDROID
// From Android's main thread; the views' is another one.
extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_UiBridge_nativeInsetsChanged(JNIEnv*, jclass)
{
    if (Ui::Platform* platform = Ui::instance.load())
        QMetaObject::invokeMethod(platform, &Ui::Platform::readInsets, Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_UiBridge_nativeAudioOpened(JNIEnv*, jclass)
{
    if (Ui::Platform* platform = Ui::instance.load())
        QMetaObject::invokeMethod(platform, &Ui::Platform::takeOpenRequests, Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_UiBridge_nativeDownloadsOpened(JNIEnv*, jclass)
{
    if (Ui::Platform* platform = Ui::instance.load())
        QMetaObject::invokeMethod(platform, &Ui::Platform::takeOpenRequests, Qt::QueuedConnection);
}

namespace {
QString fromJava(JNIEnv* env, jstring text)
{
    if (!text)
        return QString();
    const char* chars = env->GetStringUTFChars(text, nullptr);
    const QString result = QString::fromUtf8(chars);
    env->ReleaseStringUTFChars(text, chars);
    return result;
}
} // namespace

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_PhoneSignInServer_nativeSignedIn(
    JNIEnv* env, jclass, jstring request, jstring result)
{
    Ui::Platform* platform = Ui::instance.load();
    if (!platform)
        return;
    QMetaObject::invokeMethod(
        platform,
        [platform, request = fromJava(env, request), result = fromJava(env, result)] {
            emit platform->signedInWithBrowser(request, result);
        },
        Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_PhoneSignInServer_nativeExpired(JNIEnv*, jclass)
{
    if (Ui::Platform* platform = Ui::instance.load())
        QMetaObject::invokeMethod(platform, &Ui::Platform::phoneSignInEnded, Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_SignInWebActivity_nativeFinished(
    JNIEnv* env, jclass, jstring request, jstring result)
{
    Ui::Platform* platform = Ui::instance.load();
    if (!platform)
        return;
    QMetaObject::invokeMethod(
        platform,
        [platform, request = fromJava(env, request), result = fromJava(env, result)] {
            emit platform->signedInWithBrowser(request, result);
        },
        Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_PathPicker_nativePicked(
    JNIEnv* env, jclass, jstring path, jboolean cancelled)
{
    Ui::Platform* platform = Ui::instance.load();
    if (!platform)
        return;
    QMetaObject::invokeMethod(
        platform,
        [platform, path = fromJava(env, path), cancelled = bool(cancelled)] {
            emit platform->pathPicked(path, cancelled);
        },
        Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_UpdateInstaller_nativeInstallFinishedImpl(
    JNIEnv* env, jclass, jint status, jint installerStatus, jstring message)
{
    Ui::Platform* platform = Ui::instance.load();
    if (!platform)
        return;
    // UpdateInstaller.STATUS_CANCELLED is 0.
    QMetaObject::invokeMethod(
        platform,
        [platform, cancelled = status == 0, installerStatus = int(installerStatus), text = fromJava(env, message)] {
            platform->takeInstallOutcome(cancelled, installerStatus, text);
        },
        Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_StoreUpdates_nativeChecked(
    JNIEnv* env, jclass, jint versionCode, jstring error, jboolean manual)
{
    Ui::Platform* platform = Ui::instance.load();
    if (!platform)
        return;
    QMetaObject::invokeMethod(
        platform,
        [platform, code = int(versionCode), text = fromJava(env, error), asked = bool(manual)] {
            emit platform->storeUpdateChecked(code, text, asked);
        },
        Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_StoreUpdates_nativeState(
    JNIEnv* env, jclass, jint state, jlong done, jlong total, jstring error)
{
    Ui::Platform* platform = Ui::instance.load();
    if (!platform)
        return;
    QMetaObject::invokeMethod(
        platform,
        [platform, state = int(state), done = qint64(done), total = qint64(total), text = fromJava(env, error)] {
            platform->takeStoreState(state, done, total, text);
        },
        Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL Java_app_cloudmus_mobile_UpdateServer_nativeChanged(JNIEnv*, jclass)
{
    if (Ui::Platform* platform = Ui::instance.load())
        QMetaObject::invokeMethod(platform, &Ui::Platform::readUpdateServer, Qt::QueuedConnection);
}
#endif
