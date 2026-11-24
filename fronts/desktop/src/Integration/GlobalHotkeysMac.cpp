#include "GlobalHotkeysMac.h"

#include <QCoreApplication>

#include <Carbon/Carbon.h>

namespace Integration {

namespace {
constexpr OSType kSignature = 'CLMS';

// The keys that type no character, by their place on the keyboard.
UInt32 fixedKeyCodeFor(Qt::Key key)
{
    static const QHash<int, UInt32> codes {
        { Qt::Key_F1, kVK_F1 },
        { Qt::Key_F2, kVK_F2 },
        { Qt::Key_F3, kVK_F3 },
        { Qt::Key_F4, kVK_F4 },
        { Qt::Key_F5, kVK_F5 },
        { Qt::Key_F6, kVK_F6 },
        { Qt::Key_F7, kVK_F7 },
        { Qt::Key_F8, kVK_F8 },
        { Qt::Key_F9, kVK_F9 },
        { Qt::Key_F10, kVK_F10 },
        { Qt::Key_F11, kVK_F11 },
        { Qt::Key_F12, kVK_F12 },
        { Qt::Key_F13, kVK_F13 },
        { Qt::Key_F14, kVK_F14 },
        { Qt::Key_F15, kVK_F15 },
        { Qt::Key_F16, kVK_F16 },
        { Qt::Key_F17, kVK_F17 },
        { Qt::Key_F18, kVK_F18 },
        { Qt::Key_F19, kVK_F19 },
        { Qt::Key_F20, kVK_F20 },
        { Qt::Key_Space, kVK_Space },
        { Qt::Key_Return, kVK_Return },
        { Qt::Key_Enter, kVK_ANSI_KeypadEnter },
        { Qt::Key_Escape, kVK_Escape },
        { Qt::Key_Tab, kVK_Tab },
        { Qt::Key_Backspace, kVK_Delete },
        { Qt::Key_Delete, kVK_ForwardDelete },
        { Qt::Key_Insert, kVK_Help },
        { Qt::Key_Home, kVK_Home },
        { Qt::Key_End, kVK_End },
        { Qt::Key_PageUp, kVK_PageUp },
        { Qt::Key_PageDown, kVK_PageDown },
        { Qt::Key_Left, kVK_LeftArrow },
        { Qt::Key_Right, kVK_RightArrow },
        { Qt::Key_Up, kVK_UpArrow },
        { Qt::Key_Down, kVK_DownArrow },
    };
    return codes.value(int(key), UInt32(-1));
}

// The key code for each character key in the keyboard layout now (Qt's key
// codes for printable ASCII are the characters', capitalized). The layout is
// the ASCII-capable one: with a Cyrillic layout on, Ctrl+Alt+Shift+P is then
// still the key with P on it in the Latin one.
QHash<int, UInt32> characterKeyCodes()
{
    QHash<int, UInt32> codes;
    TISInputSourceRef source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    if (source == nullptr)
        return codes;
    const auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    if (data != nullptr) {
        const auto* layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
        for (UInt16 code = 0; code < 128; ++code) {
            UInt32 deadKeyState = 0;
            UniChar chars[4];
            UniCharCount length = 0;
            if (UCKeyTranslate(layout, code, kUCKeyActionDisplay, 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit,
                    &deadKeyState, 4, &length, chars)
                    != noErr
                || length != 1 || chars[0] <= 0x20 || chars[0] >= 0x7f)
                continue;
            // The first wins: the main block's digits come before the keypad's.
            codes.insert(QChar(chars[0]).toUpper().unicode(), code);
        }
    }
    CFRelease(source);
    return codes;
}

UInt32 modifiersFor(Qt::KeyboardModifiers modifiers)
{
    // Qt calls Command "Control" and Control "Meta" on macOS, unless told not to.
    const bool swapped = !QCoreApplication::testAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
    UInt32 mask = 0;
    if (modifiers.testFlag(Qt::ControlModifier))
        mask |= swapped ? cmdKey : controlKey;
    if (modifiers.testFlag(Qt::MetaModifier))
        mask |= swapped ? controlKey : cmdKey;
    if (modifiers.testFlag(Qt::AltModifier))
        mask |= optionKey;
    if (modifiers.testFlag(Qt::ShiftModifier))
        mask |= shiftKey;
    return mask;
}

OSStatus onHotKey(EventHandlerCallRef, EventRef event, void* userData)
{
    EventHotKeyID id;
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id) != noErr
        || id.signature != kSignature)
        return eventNotHandledErr;
    static_cast<GlobalHotkeysMac*>(userData)->pressed(id.id);
    return noErr;
}
} // namespace

GlobalHotkeysMac::GlobalHotkeysMac(QObject* parent)
    : GlobalHotkeys(parent)
{
    const EventTypeSpec spec { kEventClassKeyboard, kEventHotKeyPressed };
    EventHandlerRef handler = nullptr;
    InstallApplicationEventHandler(&onHotKey, 1, &spec, this, &handler);
    handler_ = handler;
}

GlobalHotkeysMac::~GlobalHotkeysMac()
{
    unregisterAll();
    if (handler_ != nullptr)
        RemoveEventHandler(static_cast<EventHandlerRef>(handler_));
}

void GlobalHotkeysMac::unregisterAll()
{
    for (void* ref : std::as_const(refs_))
        UnregisterEventHotKey(static_cast<EventHotKeyRef>(ref));
    refs_.clear();
    registered_.clear();
}

void GlobalHotkeysMac::setEntries(const QList<Entry>& entries)
{
    // Registered afresh each time: a few keys, and a changed keyboard layout
    // then also gets caught up with.
    unregisterAll();
    const QHash<int, UInt32> characterCodes = characterKeyCodes();

    QSet<QString> failed;
    for (const Entry& entry : entries) {
        if (!entry.global || entry.key.count() != 1)
            continue;
        const QKeyCombination combination = entry.key[0];
        UInt32 keyCode = fixedKeyCodeFor(combination.key());
        if (keyCode == UInt32(-1))
            keyCode = characterCodes.value(int(combination.key()), UInt32(-1));
        const quint32 number = nextNumber_++;
        EventHotKeyRef ref = nullptr;
        // Fails only for a key this app already has: the system's own
        // shortcuts and other apps' just win over it, without telling.
        if (keyCode != UInt32(-1)
            && RegisterEventHotKey(keyCode, modifiersFor(combination.keyboardModifiers()),
                   EventHotKeyID { kSignature, number }, GetApplicationEventTarget(), 0, &ref)
                == noErr) {
            refs_.append(ref);
            registered_.insert(number, entry.id);
        } else {
            failed.insert(entry.id); // a key with no place on this keyboard (media keys: Now Playing's)
        }
    }
    emit failedChanged(failed);
}

void GlobalHotkeysMac::pressed(quint32 number)
{
    const auto it = registered_.constFind(number);
    if (it != registered_.constEnd())
        emit activated(it.value(), QString());
}

} // namespace Integration
