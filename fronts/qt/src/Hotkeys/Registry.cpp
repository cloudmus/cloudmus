#include "Registry.h"

#include <QAction>

#include "Settings.h"

namespace Hotkeys {

namespace {
Binding defaultBinding(const ActionInfo& entry)
{
    return { entry.action, entry.defaultKey, entry.defaultGlobal, entry.hasNotice && entry.defaultNotify,
        entry.hasSound };
}

Binding fromConfig(const ActionInfo& entry, const Config::Settings::HotkeyConfig& config)
{
    return { entry.action, QKeySequence::fromString(config.key, QKeySequence::PortableText), config.global,
        entry.hasNotice && config.notify, entry.hasSound && config.sound };
}
} // namespace

Registry::Registry(Config::Settings& settings, QObject* parent)
    : QObject(parent)
    , settings_(settings)
{
}

QList<Binding> Registry::defaultBindings()
{
    QList<Binding> result;
    for (const ActionInfo& entry : actions())
        result.append(defaultBinding(entry));
    return result;
}

QList<Binding> Registry::bindings() const
{
    QList<Binding> result;
    for (const ActionInfo& entry : actions()) {
        const auto config = settings_.hotkey(entry.id);
        result.append(config ? fromConfig(entry, *config) : defaultBinding(entry));
    }
    return result;
}

Binding Registry::binding(Action action) const
{
    const ActionInfo& entry = info(action);
    const auto config = settings_.hotkey(entry.id);
    return config ? fromConfig(entry, *config) : defaultBinding(entry);
}

void Registry::setBindings(const QList<Binding>& bindings)
{
    const QList<Binding> before = this->bindings();
    for (const Binding& binding : bindings) {
        const ActionInfo& entry = info(binding.action);
        if (binding == defaultBinding(entry)) {
            settings_.resetHotkey(entry.id);
            continue;
        }
        settings_.setHotkey(entry.id,
            { binding.key.toString(QKeySequence::PortableText), binding.global, binding.notify, binding.sound });
    }
    if (this->bindings() != before)
        emit bindingsChanged();
}

QList<std::pair<Action, Action>> Registry::conflicts(const QList<Binding>& bindings)
{
    QList<std::pair<Action, Action>> result;
    for (int i = 0; i < bindings.size(); ++i) {
        if (bindings[i].key.isEmpty())
            continue;
        for (int j = i + 1; j < bindings.size(); ++j) {
            if (bindings[i].key == bindings[j].key)
                result.append({ bindings[i].action, bindings[j].action });
        }
    }
    return result;
}

QString Registry::keyText(Action action) const { return binding(action).key.toString(QKeySequence::NativeText); }

QString Registry::toolTip(const QString& text, Action action) const
{
    const QString key = keyText(action);
    return key.isEmpty() ? text : QStringLiteral("%1 (%2)").arg(text, key);
}

void Registry::adoptSystemKey(const QString& id, const QKeySequence& key)
{
    const ActionInfo* entry = infoById(id);
    if (entry == nullptr)
        return;
    QList<Binding> all = bindings();
    for (Binding& each : all) {
        if (each.action != entry->action)
            continue;
        // A key given in the desktop's settings is a global one, whatever
        // the app had it as.
        const bool global = each.global || !key.isEmpty();
        if (each.key == key && each.global == global)
            return;
        each.key = key;
        each.global = global;
    }
    setBindings(all);
}

void Registry::setGlobalState(GlobalSupport support, const QString& unavailableReason, bool canConfigureInSystem)
{
    if (support_ == support && reason_ == unavailableReason && canConfigure_ == canConfigureInSystem)
        return;
    support_ = support;
    reason_ = unavailableReason;
    canConfigure_ = canConfigureInSystem;
    emit globalStateChanged();
}

void Registry::setFailedIds(const QSet<QString>& ids)
{
    if (failed_ == ids)
        return;
    failed_ = ids;
    emit globalStateChanged();
}

void Registry::setSystemTriggers(const QHash<QString, QString>& triggers)
{
    if (triggers_ == triggers)
        return;
    triggers_ = triggers;
    emit globalStateChanged();
}

void showKey(QAction* menuItem, const QKeySequence& key)
{
    menuItem->setShortcut(key); // an empty one clears an earlier
    // Only with the menu itself in focus: never for the whole window.
    menuItem->setShortcutContext(Qt::WidgetShortcut);
    menuItem->setShortcutVisibleInContextMenu(true);
}

} // namespace Hotkeys
