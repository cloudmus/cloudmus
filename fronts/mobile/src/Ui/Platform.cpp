#include "Platform.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QScreen>
#include <QSysInfo>

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

QString Platform::exportLogs() const
{
#ifdef Q_OS_ANDROID
    return bridgeString("exportLogs");
#else
    return tr("The log is in %1").arg(logDirectory_);
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
