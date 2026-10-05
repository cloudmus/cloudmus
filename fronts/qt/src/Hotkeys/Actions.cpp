#include "Actions.h"

namespace Hotkeys {

namespace {
// Ctrl+Alt+Shift: free in KDE's default shortcuts, unlike the Meta+Alt group
// (Plasma, KWin and the screen reader take P, Left, Right and S).
QKeySequence chord(Qt::Key key) { return QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | key); }

} // namespace

const QList<ActionInfo>& actions()
{
    static const QList<ActionInfo> list = {
        { Action::ShowPlayer, QStringLiteral("showPlayer"), QT_TRANSLATE_NOOP("Hotkeys", "Show or hide the player"),
            chord(Qt::Key_M), true, false },
        { Action::PlayPause, QStringLiteral("playPause"), QT_TRANSLATE_NOOP("Hotkeys", "Play / pause"),
            chord(Qt::Key_P), true, false },
        { Action::Next, QStringLiteral("next"), QT_TRANSLATE_NOOP("Hotkeys", "Next track"), chord(Qt::Key_Right), true,
            false },
        { Action::Previous, QStringLiteral("previous"), QT_TRANSLATE_NOOP("Hotkeys", "Previous track"),
            chord(Qt::Key_Left), true, false },
        { Action::Stop, QStringLiteral("stop"), QT_TRANSLATE_NOOP("Hotkeys", "Stop"), chord(Qt::Key_S), true, false },
        { Action::VolumeUp, QStringLiteral("volumeUp"), QT_TRANSLATE_NOOP("Hotkeys", "Volume up"), chord(Qt::Key_Up),
            true, true },
        { Action::VolumeDown, QStringLiteral("volumeDown"), QT_TRANSLATE_NOOP("Hotkeys", "Volume down"),
            chord(Qt::Key_Down), true, true },
        { Action::ToggleQuiet, QStringLiteral("toggleQuiet"), QT_TRANSLATE_NOOP("Hotkeys", "Quiet mode on / off"),
            chord(Qt::Key_Q), true, true },
        { Action::Like, QStringLiteral("like"), QT_TRANSLATE_NOOP("Hotkeys", "Like / unlike"), chord(Qt::Key_L), true,
            true },
        { Action::Dislike, QStringLiteral("dislike"), QT_TRANSLATE_NOOP("Hotkeys", "Dislike / remove dislike"),
            chord(Qt::Key_D), true, true },
        { Action::Download, QStringLiteral("download"), QT_TRANSLATE_NOOP("Hotkeys", "Download track"), QKeySequence(),
            true, true },
    };
    return list;
}

const ActionInfo& info(Action action)
{
    for (const ActionInfo& entry : actions()) {
        if (entry.action == action)
            return entry;
    }
    return actions().first();
}

const ActionInfo* infoById(const QString& id)
{
    for (const ActionInfo& entry : actions()) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

} // namespace Hotkeys
