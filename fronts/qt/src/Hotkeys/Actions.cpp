#include "Actions.h"

#include <QCoreApplication>

namespace Hotkeys {

namespace {
// Ctrl+Alt+Shift: free in KDE's default shortcuts, unlike the Meta+Alt group
// (Plasma, KWin and the screen reader take P, Left, Right and S).
QKeySequence chord(Qt::Key key) { return QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | key); }

QString tr(const char* text) { return QCoreApplication::translate("Hotkeys::Actions", text); }
} // namespace

const QList<ActionInfo>& actions()
{
    static const QList<ActionInfo> list = {
        { Action::ShowPlayer, QStringLiteral("showPlayer"), tr("Show or hide the player"), chord(Qt::Key_M), true,
            false },
        { Action::PlayPause, QStringLiteral("playPause"), tr("Play / pause"), chord(Qt::Key_P), true, false },
        { Action::Next, QStringLiteral("next"), tr("Next track"), chord(Qt::Key_Right), true, false },
        { Action::Previous, QStringLiteral("previous"), tr("Previous track"), chord(Qt::Key_Left), true, false },
        { Action::Stop, QStringLiteral("stop"), tr("Stop"), chord(Qt::Key_S), true, false },
        { Action::VolumeUp, QStringLiteral("volumeUp"), tr("Volume up"), chord(Qt::Key_Up), true, true },
        { Action::VolumeDown, QStringLiteral("volumeDown"), tr("Volume down"), chord(Qt::Key_Down), true, true },
        { Action::ToggleQuiet, QStringLiteral("toggleQuiet"), tr("Quiet mode on / off"), chord(Qt::Key_Q), true, true },
        { Action::Like, QStringLiteral("like"), tr("Like or unlike the current track"), chord(Qt::Key_L), true, true },
        { Action::Dislike, QStringLiteral("dislike"), tr("Dislike or undo dislike of the current track"),
            chord(Qt::Key_D), true, true },
        { Action::Download, QStringLiteral("download"), tr("Download the current track"), QKeySequence(), true, true },
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
