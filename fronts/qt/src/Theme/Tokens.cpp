#include "Tokens.h"

#include <QGuiApplication>
#include <QStyleHints>

namespace Theme {

namespace {

// clang-format off
const Palette kLight {
    /* surface0      */ QColor(0xFA, 0xF7, 0xF5),
    /* surface100    */ QColor(0xFF, 0xFF, 0xFF),
    /* surface200    */ QColor(0xF1, 0xEC, 0xE9),
    /* surface300    */ QColor(0xE8, 0xE1, 0xDC),
    /* surface400    */ QColor(0xDE, 0xD5, 0xCE),
    /* border        */ QColor(0xE3, 0xDA, 0xD3),
    /* borderStrong  */ QColor(0xC9, 0xBD, 0xB4),
    /* ink           */ QColor(0x24, 0x1C, 0x17),
    /* inkSecondary  */ QColor(0x6F, 0x62, 0x59),
    /* inkTertiary   */ QColor(0x96, 0x89, 0x7E),
    /* accent        */ QColor(0xC2, 0x3D, 0x16),
    /* accentHover   */ QColor(0xA8, 0x33, 0x0F),
    /* accentPressed */ QColor(0x8E, 0x2A, 0x0C),
    /* onAccent      */ QColor(0xFF, 0xFF, 0xFF),
};

const Palette kDark {
    /* surface0      */ QColor(0x14, 0x10, 0x0D),
    /* surface100    */ QColor(0x1C, 0x17, 0x14),
    /* surface200    */ QColor(0x24, 0x1E, 0x1A),
    /* surface300    */ QColor(0x2E, 0x26, 0x21),
    /* surface400    */ QColor(0x3A, 0x30, 0x29),
    /* border        */ QColor(0x36, 0x2C, 0x25),
    /* borderStrong  */ QColor(0x4A, 0x3D, 0x34),
    /* ink           */ QColor(0xF5, 0xEF, 0xEA),
    /* inkSecondary  */ QColor(0xB4, 0xA8, 0x9D),
    /* inkTertiary   */ QColor(0x7C, 0x71, 0x67),
    /* accent        */ QColor(0xFF, 0x6A, 0x45),
    /* accentHover   */ QColor(0xFF, 0x82, 0x64),
    /* accentPressed */ QColor(0xE8, 0x5A, 0x38),
    /* onAccent      */ QColor(0x1C, 0x10, 0x06),
};
// clang-format on

} // namespace

Mode currentMode()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? Mode::Dark : Mode::Light;
}

const Palette& palette(Mode mode) { return mode == Mode::Dark ? kDark : kLight; }

Notifier::Notifier()
{
    QObject::connect(
        QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() { emit changed(); });
}

Notifier& Notifier::instance()
{
    static Notifier instance;
    return instance;
}

} // namespace Theme
