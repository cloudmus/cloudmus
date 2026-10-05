#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <utility>

#include "Actions.h"

class QAction;

namespace Config {
class Settings;
}

namespace Hotkeys {

// An action as the user has it set: the catalog's defaults, or what they
// changed.
struct Binding {
    Action action = Action::PlayPause;
    QKeySequence key;
    bool global = true;
    bool notify = true;
    bool sound = true;

    bool operator==(const Binding&) const = default;
};

// What the hotkeys are set to, and how the desktop treats the global ones —
// the one place the settings page, the in-app shortcuts and the global
// backend (Integration::GlobalHotkeys) meet, without any knowing the others.
class Registry : public QObject {
    Q_OBJECT

public:
    // How global hotkeys work here.
    enum class GlobalSupport {
        // The desktop has no way for an app to have them.
        None,
        // The app tells the desktop which key it wants (KDE, X11, Windows).
        Direct,
        // The desktop's own settings decide the key; the app's is only a
        // suggestion (the XDG portal, i.e. GNOME).
        SystemAssigned,
    };

    explicit Registry(Config::Settings& settings, QObject* parent = nullptr);

    // In the catalog's order.
    QList<Binding> bindings() const;
    Binding binding(Action action) const;
    // Saves what differs from the defaults (and forgets what no longer does).
    void setBindings(const QList<Binding>& bindings);
    static QList<Binding> defaultBindings();
    // Pairs of actions set to the same key.
    static QList<std::pair<Action, Action>> conflicts(const QList<Binding>& bindings);

    // The action's key as the user knows it ("Ctrl+Alt+Shift+L"); empty with
    // none. For showing next to what the key does:
    QString keyText(Action action) const;
    // "Like (Ctrl+Alt+Shift+L)", for a tooltip; `text` alone with no key.
    QString toolTip(const QString& text, Action action) const;

    // A key the desktop chose or the user changed there: kept as the action's
    // key without being sent back to it.
    void adoptSystemKey(const QString& id, const QKeySequence& key);

    // --- how global hotkeys are going, set by whatever provides them
    GlobalSupport globalSupport() const { return support_; }
    // Why there's no global support, when it's known.
    QString unavailableReason() const { return reason_; }
    // Whether the system can open its own editor for the keys.
    bool canConfigureInSystem() const { return canConfigure_; }
    void setGlobalState(GlobalSupport support, const QString& unavailableReason, bool canConfigureInSystem);
    // Ids of actions whose key the desktop wouldn't give (taken elsewhere).
    QSet<QString> failedIds() const { return failed_; }
    void setFailedIds(const QSet<QString>& ids);
    // What the desktop actually set the global hotkeys to, as it words it.
    QHash<QString, QString> systemTriggers() const { return triggers_; }
    void setSystemTriggers(const QHash<QString, QString>& triggers);

    // The settings page asks for the desktop's own editor of the keys; the
    // global backend answers.
    void requestConfigureInSystem() { emit configureInSystemRequested(); }

signals:
    void bindingsChanged();
    void globalStateChanged();
    void configureInSystemRequested();

private:
    Config::Settings& settings_;
    GlobalSupport support_ = GlobalSupport::None;
    QString reason_;
    bool canConfigure_ = false;
    QSet<QString> failed_;
    QHash<QString, QString> triggers_;
};

// Shows `key` at the right edge of a menu item, as the key for what the item
// does (an empty one shows nothing). It is only shown: the shortcut is active
// just while the menu is, so it never clashes with the real hotkeys.
void showKey(QAction* menuItem, const QKeySequence& key);

} // namespace Hotkeys
