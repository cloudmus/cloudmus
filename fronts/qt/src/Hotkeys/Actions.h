#pragma once

#include <QCoreApplication>
#include <QKeySequence>
#include <QList>
#include <QString>

namespace Hotkeys {

enum class Action {
    ShowPlayer,
    PlayPause,
    Next,
    Previous,
    Stop,
    VolumeUp,
    VolumeDown,
    ToggleQuiet,
    Like,
    Dislike,
    Download,
};

// One thing a hotkey can do. Everything that varies per user (the key, global
// or not, the notice) lives in Config::Settings under the `id`.
struct ActionInfo {
    Action action;
    // Stable: the config key, and the action's name in kglobalaccel and the
    // XDG portal. The first four are what the old fixed bindings were called.
    QString id;
    // The untranslated text (QT_TRANSLATE_NOOP, context "Hotkeys"); read
    // through description(), so it follows the language in effect.
    const char* descriptionSource;
    QKeySequence defaultKey;
    bool defaultGlobal = true;
    // The action can say what it did in a notification.
    bool hasNotice = false;
    // Whether that notification is on until the user turns it off.
    bool defaultNotify = true;
    // The action can say what it did with a sound — for the ones whose
    // outcome isn't obvious without seeing the window.
    bool hasSound = false;

    QString description() const { return QCoreApplication::translate("Hotkeys", descriptionSource); }
};

const QList<ActionInfo>& actions();
const ActionInfo& info(Action action);
// nullptr for an id no action has.
const ActionInfo* infoById(const QString& id);

} // namespace Hotkeys
