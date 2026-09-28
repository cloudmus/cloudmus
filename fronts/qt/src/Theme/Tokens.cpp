#include "Tokens.h"

#include <QGuiApplication>
#include <QStyleHints>

#include "WindowGlass.h"

namespace Theme {

namespace {

// clang-format off
const Palette kLight {
    // Same layout as the design system's original light values — white
    // chrome (sidebar/toolbar, surface100), a darker content area
    // (surface0) — but with far more contrast between the steps, which
    // originally were so close they blended into "almost everything
    // white". Raised controls (surface200 — menus, cards, fields, chips)
    // are white too, standing off the content area; hover/selected
    // (300/400), borders and secondary text are darker.
    /* surface0      */ QColor(0xEE, 0xE8, 0xE2),
    /* surface100    */ QColor(0xFF, 0xFF, 0xFF),
    /* surface200    */ QColor(0xFF, 0xFF, 0xFF),
    /* surface300    */ QColor(0xE2, 0xD9, 0xD1),
    /* surface400    */ QColor(0xD3, 0xC7, 0xBD),
    /* border        */ QColor(0xD6, 0xCB, 0xC2),
    /* borderStrong  */ QColor(0xB5, 0xA6, 0x9A),
    /* ink           */ QColor(0x1C, 0x15, 0x11),
    /* inkSecondary  */ QColor(0x5A, 0x4D, 0x44),
    /* inkTertiary   */ QColor(0x80, 0x71, 0x65),
    /* accent        */ QColor(0xC2, 0x3D, 0x16),
    /* accentHover   */ QColor(0xA8, 0x33, 0x0F),
    /* accentPressed */ QColor(0x8E, 0x2A, 0x0C),
    /* onAccent      */ QColor(0xFF, 0xFF, 0xFF),
    /* surfaceRaised */ QColor(0xFF, 0xFF, 0xFF),
    /* success       */ QColor(0x2E, 0x8B, 0x57),
    /* danger        */ QColor(0xC6, 0x32, 0x2B),
};

// kLight under glass: the see-through chrome turns grey over a dark
// desktop, where kLight's greys (borders, the slider groove, secondary and
// disabled icons) nearly vanish — the same roles a good step darker.
const Palette kLightGlass {
    /* surface0      */ QColor(0xEE, 0xE8, 0xE2),
    /* surface100    */ QColor(0xFF, 0xFF, 0xFF),
    /* surface200    */ QColor(0xFF, 0xFF, 0xFF),
    /* surface300    */ QColor(0xE2, 0xD9, 0xD1),
    /* surface400    */ QColor(0xD3, 0xC7, 0xBD),
    /* border        */ QColor(0x9E, 0x8F, 0x83),
    /* borderStrong  */ QColor(0x7E, 0x6F, 0x63),
    /* ink           */ QColor(0x1C, 0x15, 0x11),
    /* inkSecondary  */ QColor(0x3E, 0x33, 0x2C),
    /* inkTertiary   */ QColor(0x5A, 0x4D, 0x44),
    /* accent        */ QColor(0xC2, 0x3D, 0x16),
    /* accentHover   */ QColor(0xA8, 0x33, 0x0F),
    /* accentPressed */ QColor(0x8E, 0x2A, 0x0C),
    /* onAccent      */ QColor(0xFF, 0xFF, 0xFF),
    /* surfaceRaised */ QColor(0xFF, 0xFF, 0xFF),
    /* success       */ QColor(0x2E, 0x8B, 0x57),
    /* danger        */ QColor(0xC6, 0x32, 0x2B),
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
    /* surfaceRaised */ QColor(0x36, 0x2D, 0x27),
    /* success       */ QColor(0x5C, 0xC2, 0x8A),
    /* danger        */ QColor(0xFF, 0x5F, 0x5A),
};
// clang-format on

bool glassEnabled_ = false;

} // namespace

void setGlassEnabled(bool enabled)
{
    if (glassEnabled_ == enabled)
        return;
    glassEnabled_ = enabled;
    emit notifier().glassChanged();
    emit notifier().changed();
}

bool glassEnabled() { return glassEnabled_; }

bool glassByDefault()
{
    // Only where the desktop itself blurs: elsewhere glass is just a
    // see-through window unless the user set up something to blur it.
    return currentMode() == Mode::Dark
        && Integration::WindowGlass::support() == Integration::WindowGlass::Support::Blur;
}

QColor glass(const QColor& color, qreal opacity)
{
    if (!glassEnabled_)
        return color;
    // A pale tint hides the blur behind it far more than a dark one at the
    // same opacity — the light scheme's glass only reads as glass at a
    // thinner tint.
    if (currentMode() == Mode::Light)
        opacity -= 0.05;
    QColor tinted = color;
    tinted.setAlphaF(opacity);
    return tinted;
}

namespace {
std::optional<Mode> modeOverride;
}

void setModeOverride(std::optional<Mode> mode)
{
    if (modeOverride == mode)
        return;
    modeOverride = mode;
    emit notifier().changed();
}

QColor over(const QColor& top, const QColor& bottom)
{
    const qreal topAlpha = top.alphaF();
    const qreal bottomAlpha = bottom.alphaF() * (1.0 - topAlpha);
    const qreal alpha = topAlpha + bottomAlpha;
    if (alpha <= 0.0)
        return Qt::transparent;
    const auto channel = [&](qreal t, qreal b) { return (t * topAlpha + b * bottomAlpha) / alpha; };
    return QColor::fromRgbF(float(channel(top.redF(), bottom.redF())), float(channel(top.greenF(), bottom.greenF())),
        float(channel(top.blueF(), bottom.blueF())), float(alpha));
}

Mode currentMode()
{
    if (modeOverride)
        return *modeOverride;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? Mode::Dark : Mode::Light;
}

const Palette& palette(Mode mode)
{
    if (mode == Mode::Dark)
        return kDark;
    return glassEnabled_ ? kLightGlass : kLight;
}

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
