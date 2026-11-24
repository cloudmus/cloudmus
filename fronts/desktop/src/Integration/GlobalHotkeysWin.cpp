#include "GlobalHotkeysWin.h"

#include <QCoreApplication>

#include <windows.h>

namespace Integration {

namespace {
// The virtual-key code for a Qt key; 0 for none.
UINT virtualKeyFor(Qt::Key key)
{
    const int code = int(key);
    if (code >= Qt::Key_A && code <= Qt::Key_Z)
        return UINT(code - Qt::Key_A + 'A');
    if (code >= Qt::Key_0 && code <= Qt::Key_9)
        return UINT(code - Qt::Key_0 + '0');
    if (code >= Qt::Key_F1 && code <= Qt::Key_F24)
        return UINT(VK_F1 + (code - Qt::Key_F1));
    switch (key) {
        case Qt::Key_Space:
            return VK_SPACE;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return VK_RETURN;
        case Qt::Key_Escape:
            return VK_ESCAPE;
        case Qt::Key_Tab:
            return VK_TAB;
        case Qt::Key_Backspace:
            return VK_BACK;
        case Qt::Key_Insert:
            return VK_INSERT;
        case Qt::Key_Delete:
            return VK_DELETE;
        case Qt::Key_Home:
            return VK_HOME;
        case Qt::Key_End:
            return VK_END;
        case Qt::Key_PageUp:
            return VK_PRIOR;
        case Qt::Key_PageDown:
            return VK_NEXT;
        case Qt::Key_Left:
            return VK_LEFT;
        case Qt::Key_Up:
            return VK_UP;
        case Qt::Key_Right:
            return VK_RIGHT;
        case Qt::Key_Down:
            return VK_DOWN;
        case Qt::Key_Comma:
            return VK_OEM_COMMA;
        case Qt::Key_Period:
            return VK_OEM_PERIOD;
        case Qt::Key_Minus:
            return VK_OEM_MINUS;
        case Qt::Key_Equal:
        case Qt::Key_Plus:
            return VK_OEM_PLUS;
        case Qt::Key_MediaPlay:
        case Qt::Key_MediaTogglePlayPause:
            return VK_MEDIA_PLAY_PAUSE;
        case Qt::Key_MediaStop:
            return VK_MEDIA_STOP;
        case Qt::Key_MediaPrevious:
            return VK_MEDIA_PREV_TRACK;
        case Qt::Key_MediaNext:
            return VK_MEDIA_NEXT_TRACK;
        case Qt::Key_VolumeUp:
            return VK_VOLUME_UP;
        case Qt::Key_VolumeDown:
            return VK_VOLUME_DOWN;
        case Qt::Key_VolumeMute:
            return VK_VOLUME_MUTE;
        default:
            return 0;
    }
}

UINT modifiersFor(Qt::KeyboardModifiers modifiers)
{
    UINT mask = MOD_NOREPEAT; // a held key is one press
    if (modifiers.testFlag(Qt::ControlModifier))
        mask |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier))
        mask |= MOD_ALT;
    if (modifiers.testFlag(Qt::ShiftModifier))
        mask |= MOD_SHIFT;
    if (modifiers.testFlag(Qt::MetaModifier))
        mask |= MOD_WIN;
    return mask;
}
} // namespace

GlobalHotkeysWin::GlobalHotkeysWin(QObject* parent)
    : GlobalHotkeys(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkeysWin::~GlobalHotkeysWin()
{
    unregisterAll();
    if (QCoreApplication::instance() != nullptr)
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

void GlobalHotkeysWin::unregisterAll()
{
    for (auto it = registered_.begin(); it != registered_.end(); ++it)
        UnregisterHotKey(nullptr, it.key());
    registered_.clear();
}

void GlobalHotkeysWin::setEntries(const QList<Entry>& entries)
{
    unregisterAll();
    QSet<QString> failed;
    for (const Entry& entry : entries) {
        if (!entry.global || entry.key.count() != 1)
            continue;
        const QKeyCombination combination = entry.key[0];
        const UINT virtualKey = virtualKeyFor(combination.key());
        const int number = nextNumber_++;
        // No window: the message goes to the thread, see nativeEventFilter().
        if (virtualKey != 0
            && RegisterHotKey(nullptr, number, modifiersFor(combination.keyboardModifiers()), virtualKey))
            registered_.insert(number, entry.id);
        else
            failed.insert(entry.id); // another program has it, or it has no virtual key
    }
    emit failedChanged(failed);
}

bool GlobalHotkeysWin::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
    // A message for no window is only seen by the dispatcher.
    if (eventType != "windows_dispatcher_MSG" && eventType != "windows_generic_MSG")
        return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_HOTKEY)
        return false;
    const auto it = registered_.constFind(int(msg->wParam));
    if (it == registered_.constEnd())
        return false;
    emit activated(it.value(), QString());
    return true;
}

} // namespace Integration
