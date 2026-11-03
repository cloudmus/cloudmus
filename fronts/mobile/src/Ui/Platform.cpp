#include "Platform.h"

#include <QElapsedTimer>
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

#include <memory>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#include "Log.h"

namespace Ui {

namespace {

Q_LOGGING_CATEGORY(lcPlatform, "cloudmus.ui.platform")

#ifdef Q_OS_ANDROID
QString bridgeString(const char* method)
{
    return QJniObject::callStaticMethod<jstring>(
        "dev/cloudmus/mobile/UiBridge", method, QNativeInterface::QAndroidApplication::context())
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
#endif
}

// The export can't be stopped halfway; quitting waits for it rather than
// destroying a running thread.
Platform::~Platform()
{
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
                qCInfo(lcPlatform) << "python probe:" << clock->elapsed() << "ms:"
                                   << QString::fromUtf8(process->readLine()).trimmed();
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

void Platform::startService()
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(
        "dev/cloudmus/mobile/UiBridge", "startService", QNativeInterface::QAndroidApplication::context());
#endif
}

} // namespace Ui
