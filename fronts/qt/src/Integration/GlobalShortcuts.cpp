#include "GlobalShortcuts.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QKeySequence>

namespace Integration {

namespace {
constexpr const char* kServiceName = "org.kde.kglobalaccel";
constexpr const char* kComponentUnique = "cloudmus";
} // namespace

GlobalShortcuts::GlobalShortcuts(QObject* parent)
    : QObject(parent)
{
    if (!QDBusConnection::sessionBus().interface()->isServiceRegistered(QString::fromLatin1(kServiceName))) {
        return; // no KDE global-shortcut daemon on this session bus — graceful no-op, see the header's doc comment
    }

    registerAction(QStringLiteral("playPause"), tr("Play/Pause"), QStringLiteral("Meta+Alt+P"));
    registerAction(QStringLiteral("next"), tr("Next track"), QStringLiteral("Meta+Alt+Right"));
    registerAction(QStringLiteral("previous"), tr("Previous track"), QStringLiteral("Meta+Alt+Left"));
    registerAction(QStringLiteral("stop"), tr("Stop"), QStringLiteral("Meta+Alt+S"));

    const QString componentPath = QStringLiteral("/component/%1").arg(QString::fromLatin1(kComponentUnique));
    QDBusConnection::sessionBus().connect(
        QString::fromLatin1(kServiceName), componentPath, QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("globalShortcutPressed"), this, SLOT(onGlobalShortcutPressed(QString, QString, qlonglong)));
}

void GlobalShortcuts::registerAction(const QString& actionId, const QString& friendlyName,
                                     const QString& defaultShortcut)
{
    const QStringList actionIdParts { QString::fromLatin1(kComponentUnique), actionId, QStringLiteral("cloudmus"),
                                      friendlyName };

    QDBusInterface kglobalaccel(QString::fromLatin1(kServiceName), QStringLiteral("/kglobalaccel"),
                                QStringLiteral("org.kde.KGlobalAccel"));
    kglobalaccel.call(QStringLiteral("doRegister"), QVariant::fromValue(actionIdParts));

    const int key = QKeySequence(defaultShortcut)[0].toCombined();
    constexpr uint kSetPresentFlag = 0x4;
    kglobalaccel.call(QStringLiteral("setShortcut"), QVariant::fromValue(actionIdParts),
                      QVariant::fromValue(QList<int> { key }), kSetPresentFlag);
}

void GlobalShortcuts::onGlobalShortcutPressed(const QString& componentUnique, const QString& actionUnique,
                                              qlonglong timestamp)
{
    Q_UNUSED(timestamp);
    if (componentUnique != QString::fromLatin1(kComponentUnique))
        return;
    if (actionUnique == QStringLiteral("playPause"))
        emit playPauseTriggered();
    else if (actionUnique == QStringLiteral("next"))
        emit nextTriggered();
    else if (actionUnique == QStringLiteral("previous"))
        emit previousTriggered();
    else if (actionUnique == QStringLiteral("stop"))
        emit stopTriggered();
}

} // namespace Integration
