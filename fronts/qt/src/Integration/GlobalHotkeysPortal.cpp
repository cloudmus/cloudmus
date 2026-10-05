#include "GlobalHotkeysPortal.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUuid>

#include "KeyNames.h"

namespace Integration {

Q_LOGGING_CATEGORY(lcHotkeysPortal, "cloudmus.integration.hotkeys.portal")

namespace {
constexpr const char* kService = "org.freedesktop.portal.Desktop";
constexpr const char* kPath = "/org/freedesktop/portal/desktop";
constexpr const char* kInterface = "org.freedesktop.portal.GlobalShortcuts";
// The app's id for the portal: the basename of its .desktop file.
constexpr const char* kAppId = "cloudmus-qt";

// One (sa{sv}) of the portal's shortcut lists.
struct PortalShortcut {
    QString id;
    QVariantMap options;
};

QDBusArgument& operator<<(QDBusArgument& argument, const PortalShortcut& shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.options;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PortalShortcut& shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.options;
    argument.endStructure();
    return argument;
}

void registerTypes()
{
    static const bool once = [] {
        qDBusRegisterMetaType<PortalShortcut>();
        qDBusRegisterMetaType<QList<PortalShortcut>>();
        return true;
    }();
    Q_UNUSED(once);
}

QString objectPathOf(const QVariant& value)
{
    if (value.userType() == qMetaTypeId<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    return value.toString();
}
} // namespace

uint GlobalHotkeysPortal::availableVersion()
{
    QDBusConnectionInterface* bus = QDBusConnection::sessionBus().interface();
    if (bus == nullptr)
        return 0;
    // Asking also starts a portal that is only activatable.
    QDBusInterface properties(QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::sessionBus());
    properties.setTimeout(2000);
    const QDBusMessage reply
        = properties.call(QStringLiteral("Get"), QString::fromLatin1(kInterface), QStringLiteral("version"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return 0;
    return reply.arguments().first().value<QDBusVariant>().variant().toUInt();
}

GlobalHotkeysPortal::GlobalHotkeysPortal(uint version, QObject* parent)
    : GlobalHotkeys(parent)
    , connection_(QStringLiteral("cloudmus-hotkeys-portal"))
    , connectionName_(QStringLiteral("cloudmus-hotkeys-portal"))
    , version_(version)
{
    registerTypes();
    connection_ = QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName_);
    registerApp();

    connection_.connect(QString::fromLatin1(kService), QString(), QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"), this, SLOT(onResponse(QDBusMessage)));
    connection_.connect(QString::fromLatin1(kService), QString::fromLatin1(kPath), QString::fromLatin1(kInterface),
        QStringLiteral("Activated"), this, SLOT(onActivated(QDBusMessage)));
    connection_.connect(QString::fromLatin1(kService), QString::fromLatin1(kPath), QString::fromLatin1(kInterface),
        QStringLiteral("ShortcutsChanged"), this, SLOT(onShortcutsChanged(QDBusMessage)));

    watcher_ = new QDBusServiceWatcher(
        QString::fromLatin1(kService), connection_, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher_, &QDBusServiceWatcher::serviceOwnerChanged, this, &GlobalHotkeysPortal::onPortalOwnerChanged);
}

GlobalHotkeysPortal::~GlobalHotkeysPortal()
{
    closeSession();
    QDBusConnection::disconnectFromBus(connectionName_);
}

void GlobalHotkeysPortal::registerApp()
{
    // Errors are fine: a portal older than 1.20 has no Registry, and for one
    // that has it, a connection already registered is told so.
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QStringLiteral("org.freedesktop.host.portal.Registry"), QStringLiteral("Register"));
    message << QString::fromLatin1(kAppId) << QVariantMap();
    const QDBusMessage reply = connection_.call(message, QDBus::Block, 2000);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qCDebug(lcHotkeysPortal) << "Registry.Register:" << reply.errorMessage();
}

QString GlobalHotkeysPortal::newToken() { return QStringLiteral("cloudmus%1").arg(++tokenCounter_); }

QString GlobalHotkeysPortal::requestPath(const QString& token) const
{
    // The sender's unique name without the colon, dots turned to underscores.
    QString sender = connection_.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

void GlobalHotkeysPortal::callWithRequest(const QString& method, const QList<QVariant>& leading, QVariantMap options,
    const ResponseHandler& handler, const std::function<void(const QString&)>& onError)
{
    const QString token = newToken();
    options.insert(QStringLiteral("handle_token"), token);
    // Listening comes first: the answer may come before the call returns.
    pending_.insert(requestPath(token), handler);

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kPath), QString::fromLatin1(kInterface), method);
    for (const QVariant& argument : leading)
        message << argument;
    message << options;
    auto* watcher = new QDBusPendingCallWatcher(connection_.asyncCall(message), this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, token, onError](QDBusPendingCallWatcher*) {
            watcher->deleteLater();
            const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
            if (reply.isError()) {
                pending_.remove(requestPath(token));
                onError(reply.error().message());
            }
        });
}

void GlobalHotkeysPortal::onResponse(const QDBusMessage& message)
{
    auto handler = pending_.take(message.path());
    if (!handler || message.arguments().size() < 2)
        return;
    handler(message.arguments().at(0).toUInt(), qdbus_cast<QVariantMap>(message.arguments().at(1)));
}

void GlobalHotkeysPortal::setEntries(const QList<Entry>& entries)
{
    entries_ = entries;
    QStringList signature;
    for (const Entry& entry : entries) {
        if (entry.global)
            signature << entry.id + QLatin1Char('=') + entry.description;
    }
    const QString wanted = signature.join(QLatin1Char('\n'));
    if (wanted == boundSignature_ && (!session_.isEmpty() || support_ == Hotkeys::Registry::GlobalSupport::None))
        return;
    // BindShortcuts can be made once per session: another set of actions
    // means another session.
    closeSession();
    boundSignature_ = wanted;
    if (!wanted.isEmpty())
        createSession();
}

void GlobalHotkeysPortal::createSession()
{
    const quint64 generation = ++generation_;
    const QString token = newToken();
    QVariantMap options;
    options.insert(QStringLiteral("session_handle_token"), token + QLatin1Char('s'));
    callWithRequest(
        QStringLiteral("CreateSession"), { }, options,
        [this, generation](uint code, const QVariantMap& results) {
            if (generation != generation_)
                return;
            if (code != 0 || !results.contains(QStringLiteral("session_handle"))) {
                fail(tr("The desktop did not open a global shortcuts session."));
                return;
            }
            session_ = objectPathOf(results.value(QStringLiteral("session_handle")));
            bindShortcuts(generation);
        },
        [this, generation](const QString& error) {
            if (generation != generation_)
                return;
            qCDebug(lcHotkeysPortal) << "CreateSession:" << error;
            const bool installed = !QStandardPaths::locate(
                QStandardPaths::ApplicationsLocation, QString::fromLatin1(kAppId) + QStringLiteral(".desktop"))
                                        .isEmpty();
            fail(installed ? tr("The desktop refused global shortcuts: %1").arg(error)
                           : tr("This copy of CloudMus isn't installed (there is no %1.desktop file), so the "
                                "desktop won't give it global shortcuts.")
                                 .arg(QString::fromLatin1(kAppId)));
        });
}

void GlobalHotkeysPortal::bindShortcuts(quint64 generation)
{
    QList<PortalShortcut> shortcuts;
    for (const Entry& entry : std::as_const(entries_)) {
        if (!entry.global)
            continue;
        PortalShortcut shortcut;
        shortcut.id = entry.id;
        shortcut.options.insert(QStringLiteral("description"), entry.description);
        // Only a wish: the desktop keeps what it already has for the action.
        const QString trigger = portalTrigger(entry.key);
        if (!trigger.isEmpty())
            shortcut.options.insert(QStringLiteral("preferred_trigger"), trigger);
        shortcuts.append(shortcut);
    }

    callWithRequest(
        QStringLiteral("BindShortcuts"),
        { QVariant::fromValue(QDBusObjectPath(session_)), QVariant::fromValue(shortcuts), QString() }, { },
        [this, generation](uint code, const QVariantMap& results) {
            if (generation != generation_)
                return;
            if (code == 2) {
                fail(tr("The desktop could not set up the global shortcuts."));
                return;
            }
            // 1: the user closed the question without answering — the keys
            // stay unset, and asked for again the next time.
            bound_ = code == 0;
            setTriggers(results.value(QStringLiteral("shortcuts")));
            emit stateChanged();
        },
        [this, generation](const QString& error) {
            if (generation != generation_)
                return;
            qCDebug(lcHotkeysPortal) << "BindShortcuts:" << error;
            fail(tr("The desktop could not set up the global shortcuts: %1").arg(error));
        });
}

void GlobalHotkeysPortal::setTriggers(const QVariant& shortcuts)
{
    triggers_.clear();
    for (const PortalShortcut& shortcut : qdbus_cast<QList<PortalShortcut>>(shortcuts))
        triggers_.insert(shortcut.id, shortcut.options.value(QStringLiteral("trigger_description")).toString());
    emit systemTriggersChanged(triggers_);
}

void GlobalHotkeysPortal::onShortcutsChanged(const QDBusMessage& message)
{
    if (message.arguments().size() < 2 || objectPathOf(message.arguments().at(0)) != session_)
        return;
    setTriggers(message.arguments().at(1));
}

void GlobalHotkeysPortal::onActivated(const QDBusMessage& message)
{
    // session, shortcut id, timestamp, options
    if (message.arguments().size() < 4 || objectPathOf(message.arguments().at(0)) != session_)
        return;
    const QVariantMap options = qdbus_cast<QVariantMap>(message.arguments().at(3));
    emit activated(message.arguments().at(1).toString(), options.value(QStringLiteral("activation_token")).toString());
}

void GlobalHotkeysPortal::closeSession()
{
    ++generation_;
    pending_.clear();
    if (!session_.isEmpty()) {
        connection_.asyncCall(QDBusMessage::createMethodCall(QString::fromLatin1(kService), session_,
            QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close")));
    }
    session_.clear();
    bound_ = false;
    if (!triggers_.isEmpty()) {
        triggers_.clear();
        emit systemTriggersChanged(triggers_);
    }
}

void GlobalHotkeysPortal::fail(const QString& why)
{
    session_.clear();
    bound_ = false;
    support_ = Hotkeys::Registry::GlobalSupport::None;
    reason_ = why;
    emit stateChanged();
}

void GlobalHotkeysPortal::configureInSystem()
{
    if (!canConfigureInSystem())
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QString::fromLatin1(kInterface), QStringLiteral("ConfigureShortcuts"));
    message << QVariant::fromValue(QDBusObjectPath(session_)) << QString() << QVariantMap();
    connection_.asyncCall(message);
}

void GlobalHotkeysPortal::onPortalOwnerChanged(const QString&, const QString&, const QString& newOwner)
{
    if (newOwner.isEmpty())
        return;
    // A restarted portal knows neither our id nor the session.
    registerApp();
    const bool had = !boundSignature_.isEmpty();
    closeSession();
    support_ = Hotkeys::Registry::GlobalSupport::SystemAssigned;
    reason_.clear();
    if (had)
        createSession();
    emit stateChanged();
}

} // namespace Integration
