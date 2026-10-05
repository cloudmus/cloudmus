#include "KeyNames.h"

#include <QStringList>

namespace Integration {

namespace {
struct KeyName {
    Qt::Key key;
    unsigned int keysym;
    const char* name;
};

// Keys whose keysym isn't their Qt code.
constexpr KeyName kNamed[] = {
    { Qt::Key_Space, 0x0020, "space" },
    { Qt::Key_Escape, 0xff1b, "Escape" },
    { Qt::Key_Tab, 0xff09, "Tab" },
    { Qt::Key_Backspace, 0xff08, "BackSpace" },
    { Qt::Key_Return, 0xff0d, "Return" },
    { Qt::Key_Enter, 0xff8d, "KP_Enter" },
    { Qt::Key_Insert, 0xff63, "Insert" },
    { Qt::Key_Delete, 0xffff, "Delete" },
    { Qt::Key_Home, 0xff50, "Home" },
    { Qt::Key_End, 0xff57, "End" },
    { Qt::Key_Left, 0xff51, "Left" },
    { Qt::Key_Up, 0xff52, "Up" },
    { Qt::Key_Right, 0xff53, "Right" },
    { Qt::Key_Down, 0xff54, "Down" },
    { Qt::Key_PageUp, 0xff55, "Page_Up" },
    { Qt::Key_PageDown, 0xff56, "Page_Down" },
    { Qt::Key_MediaPlay, 0x1008ff14, "XF86AudioPlay" },
    { Qt::Key_MediaStop, 0x1008ff15, "XF86AudioStop" },
    { Qt::Key_MediaPrevious, 0x1008ff16, "XF86AudioPrev" },
    { Qt::Key_MediaNext, 0x1008ff17, "XF86AudioNext" },
    { Qt::Key_VolumeUp, 0x1008ff13, "XF86AudioRaiseVolume" },
    { Qt::Key_VolumeDown, 0x1008ff11, "XF86AudioLowerVolume" },
    { Qt::Key_VolumeMute, 0x1008ff12, "XF86AudioMute" },
    // ASCII punctuation, whose keysym names are words.
    { Qt::Key_Comma, 0x002c, "comma" },
    { Qt::Key_Period, 0x002e, "period" },
    { Qt::Key_Minus, 0x002d, "minus" },
    { Qt::Key_Equal, 0x003d, "equal" },
    { Qt::Key_Slash, 0x002f, "slash" },
    { Qt::Key_Semicolon, 0x003b, "semicolon" },
    { Qt::Key_Apostrophe, 0x0027, "apostrophe" },
    { Qt::Key_BracketLeft, 0x005b, "bracketleft" },
    { Qt::Key_BracketRight, 0x005d, "bracketright" },
    { Qt::Key_Backslash, 0x005c, "backslash" },
    { Qt::Key_QuoteLeft, 0x0060, "grave" },
};
} // namespace

unsigned int keysymForKey(Qt::Key key)
{
    for (const KeyName& entry : kNamed) {
        if (entry.key == key)
            return entry.keysym;
    }
    const int code = int(key);
    if (code >= Qt::Key_F1 && code <= Qt::Key_F35)
        return 0xffbe + (code - Qt::Key_F1);
    if (code >= Qt::Key_A && code <= Qt::Key_Z)
        return unsigned(code - Qt::Key_A + 'a'); // lower case: the key, not the letter typed
    if (code >= Qt::Key_0 && code <= Qt::Key_9)
        return unsigned(code);
    return 0;
}

QString keysymNameForKey(Qt::Key key)
{
    for (const KeyName& entry : kNamed) {
        if (entry.key == key)
            return QString::fromLatin1(entry.name);
    }
    const int code = int(key);
    if (code >= Qt::Key_F1 && code <= Qt::Key_F35)
        return QStringLiteral("F%1").arg(code - Qt::Key_F1 + 1);
    if (code >= Qt::Key_A && code <= Qt::Key_Z)
        return QString(QChar(code - Qt::Key_A + 'a'));
    if (code >= Qt::Key_0 && code <= Qt::Key_9)
        return QString(QChar(code));
    return { };
}

QString portalTrigger(const QKeySequence& sequence)
{
    if (sequence.count() != 1)
        return { };
    const QKeyCombination combination = sequence[0];
    const QString name = keysymNameForKey(combination.key());
    if (name.isEmpty())
        return { };
    QStringList parts;
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    if (modifiers.testFlag(Qt::ControlModifier))
        parts << QStringLiteral("CTRL");
    if (modifiers.testFlag(Qt::AltModifier))
        parts << QStringLiteral("ALT");
    if (modifiers.testFlag(Qt::ShiftModifier))
        parts << QStringLiteral("SHIFT");
    // Qt's "Meta" is the Windows/Super key.
    if (modifiers.testFlag(Qt::MetaModifier))
        parts << QStringLiteral("LOGO");
    parts << name;
    return parts.join(QLatin1Char('+'));
}

} // namespace Integration
