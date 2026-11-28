#include "GlobalHotkeysKGlobalAccel.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QTimer>

namespace Integration {

namespace {
constexpr const char* kService = "org.kde.kglobalaccel";
constexpr const char* kPath = "/kglobalaccel";
constexpr const char* kInterface = "org.kde.KGlobalAccel";
// The component is named like the app's .desktop file: that is where
// System Settings takes the name and icon from.
constexpr const char* kComponent = "cloudmus-qt";
constexpr const char* kOldComponent = "cloudmus";

// KGlobalAccel's setShortcut flags (kglobalaccel_p.h).
constexpr uint kSetPresent = 0x2;
constexpr uint kNoAutoloading = 0x4;
constexpr uint kIsDefault = 0x8;

QDBusInterface daemon()
{
    return QDBusInterface(QString::fromLatin1(kService), QString::fromLatin1(kPath), QString::fromLatin1(kInterface),
        QDBusConnection::sessionBus());
}

QList<int> intsOf(const QKeySequence& key)
{
    if (key.isEmpty())
        return { };
    return { key[0].toCombined() };
}

QKeySequence keyOf(const QList<int>& ints)
{
    return ints.isEmpty() || ints.first() == 0 ? QKeySequence() : QKeySequence(ints.first());
}
} // namespace

bool GlobalHotkeysKGlobalAccel::available()
{
    QDBusConnectionInterface* bus = QDBusConnection::sessionBus().interface();
    return bus != nullptr && bus->isServiceRegistered(QString::fromLatin1(kService));
}

GlobalHotkeysKGlobalAccel::GlobalHotkeysKGlobalAccel(QObject* parent)
    : GlobalHotkeys(parent)
{
    // The daemon tells every client about a change of keys, including the
    // ones made in System Settings.
    QDBusConnection::sessionBus().connect(QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QString::fromLatin1(kInterface), QStringLiteral("yourShortcutsChanged"), this,
        SLOT(onShortcutsChanged(QDBusMessage)));
    migrateOldComponent();
}

QStringList GlobalHotkeysKGlobalAccel::actionId(const Entry& entry) const
{
    // component, action, component's friendly name, action's friendly name.
    return { QString::fromLatin1(kComponent), entry.id, QStringLiteral("CloudMus"), entry.description };
}

QList<int> GlobalHotkeysKGlobalAccel::setShortcut(const QStringList& actionId, const QList<int>& keys, uint flags)
{
    QDBusInterface iface = daemon();
    const QDBusMessage reply
        = iface.call(QStringLiteral("setShortcut"), actionId, QVariant::fromValue(keys), QVariant::fromValue(flags));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return { };
    return qdbus_cast<QList<int>>(reply.arguments().first());
}

void GlobalHotkeysKGlobalAccel::postKey(const QString& id, const QKeySequence& key)
{
    // Not from inside setEntries(): the receiver answers with setEntries().
    QTimer::singleShot(0, this, [this, id, key]() { emit systemKeyChanged(id, key); });
}

void GlobalHotkeysKGlobalAccel::setEntries(const QList<Entry>& entries)
{
    QDBusInterface iface = daemon();
    QSet<QString> failed;
    for (const Entry& entry : entries) {
        const QStringList id = actionId(entry);
        const bool known = current_.contains(entry.id);
        const QKeySequence wanted = entry.global ? entry.key : QKeySequence();

        if (!known) {
            iface.call(QStringLiteral("doRegister"), id);
            setShortcut(id, intsOf(entry.key), kIsDefault);
            // Not global: let go of whatever the daemon remembers.
            // Global: its remembered key (the user may have changed it in
            // System Settings while the app wasn't running) wins over ours.
            const QKeySequence got = keyOf(setShortcut(id, entry.global ? intsOf(entry.key) : QList<int>(),
                entry.global ? kSetPresent : (kSetPresent | kNoAutoloading)));
            current_.insert(entry.id, got);
            if (entry.global) {
                if (got != entry.key && !got.isEmpty())
                    postKey(entry.id, got);
                else if (got.isEmpty() && !entry.key.isEmpty())
                    failed.insert(entry.id);
            }
            continue;
        }

        if (current_.value(entry.id) == wanted) {
            if (failed_.contains(entry.id) && entry.global)
                failed.insert(entry.id);
            continue;
        }
        const QKeySequence got = keyOf(setShortcut(id, intsOf(wanted), kSetPresent | kNoAutoloading));
        current_.insert(entry.id, got);
        if (entry.global && got.isEmpty() && !wanted.isEmpty())
            failed.insert(entry.id);
    }

    connectComponent();
    if (failed != failed_) {
        failed_ = failed;
        emit failedChanged(failed_);
    }
}

void GlobalHotkeysKGlobalAccel::connectComponent()
{
    if (componentConnected_)
        return;
    QDBusInterface iface = daemon();
    // Its path escapes the dash of the name; ask rather than build it.
    const QDBusReply<QDBusObjectPath> path
        = iface.call(QStringLiteral("getComponent"), QString::fromLatin1(kComponent));
    if (!path.isValid())
        return;
    componentConnected_ = QDBusConnection::sessionBus().connect(QString::fromLatin1(kService), path.value().path(),
        QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("globalShortcutPressed"), this,
        SLOT(onShortcutPressed(QString, QString, qlonglong)));
}

void GlobalHotkeysKGlobalAccel::onShortcutPressed(
    const QString& componentUnique, const QString& actionUnique, qlonglong timestamp)
{
    Q_UNUSED(timestamp);
    if (componentUnique == QLatin1String(kComponent))
        emit activated(actionUnique, QString());
}

void GlobalHotkeysKGlobalAccel::onShortcutsChanged(const QDBusMessage& message)
{
    if (message.arguments().size() < 2)
        return;
    const QStringList id = message.arguments().at(0).toStringList();
    if (id.size() < 2 || id.first() != QLatin1String(kComponent))
        return;

    // a(ai): a list of key sequences, each a list of up to four keys.
    QKeySequence key;
    const QDBusArgument sequences = message.arguments().at(1).value<QDBusArgument>();
    sequences.beginArray();
    if (!sequences.atEnd()) {
        sequences.beginStructure();
        QList<int> ints;
        sequences.beginArray();
        while (!sequences.atEnd()) {
            int value = 0;
            sequences >> value;
            ints.append(value);
        }
        sequences.endArray();
        sequences.endStructure();
        key = keyOf(ints);
    }
    // (the rest of the list, if any, is of no use: one chord per action)

    const QString actionId = id.at(1);
    if (!current_.contains(actionId) || current_.value(actionId) == key)
        return; // not ours to react to, or our own doing
    current_.insert(actionId, key);
    if (!key.isEmpty() && failed_.remove(actionId))
        emit failedChanged(failed_);
    emit systemKeyChanged(actionId, key);
}

void GlobalHotkeysKGlobalAccel::migrateOldComponent()
{
    QDBusInterface iface = daemon();
    const QDBusReply<QList<QDBusObjectPath>> components = iface.call(QStringLiteral("allComponents"));
    if (!components.isValid())
        return;
    const QString oldPath = QStringLiteral("/component/") + QString::fromLatin1(kOldComponent);
    bool found = false;
    for (const QDBusObjectPath& path : components.value())
        found = found || path.path() == oldPath;
    if (!found)
        return;

    // The defaults the old fixed bindings had: a key that differs was the
    // user's own, and carries over.
    struct Old {
        const char* id;
        const char* byDefault;
    };
    constexpr Old kOld[] = { { "playPause", "Meta+Alt+P" }, { "next", "Meta+Alt+Right" },
        { "previous", "Meta+Alt+Left" }, { "stop", "Meta+Alt+S" } };
    for (const Old& old : kOld) {
        const QStringList id { QString::fromLatin1(kOldComponent), QString::fromLatin1(old.id), QString(), QString() };
        const QDBusMessage reply = iface.call(QStringLiteral("shortcut"), id);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QKeySequence key = keyOf(qdbus_cast<QList<int>>(reply.arguments().first()));
            if (!key.isEmpty() && key != QKeySequence(QString::fromLatin1(old.byDefault)))
                postKey(QString::fromLatin1(old.id), key);
        }
        iface.call(QStringLiteral("unRegister"), id);
    }
    QDBusInterface component(QString::fromLatin1(kService), oldPath, QStringLiteral("org.kde.kglobalaccel.Component"),
        QDBusConnection::sessionBus());
    component.call(QStringLiteral("cleanUp"));
}

} // namespace Integration
