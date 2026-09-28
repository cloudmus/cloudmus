#pragma once

#include <QColor>
#include <QObject>

#include <optional>

namespace Theme {

enum class Mode {
    Light,
    Dark
};

// One entry per color role in the CloudMus design system. Values are
// transcribed verbatim from the design system's tokens.json — see each
// member's design-system name in the .cpp.
struct Palette {
    QColor surface0;
    QColor surface100;
    QColor surface200;
    QColor surface300;
    QColor surface400;
    QColor border;
    QColor borderStrong;
    QColor ink;
    QColor inkSecondary;
    QColor inkTertiary;
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor onAccent;
    // Floating over whatever is under it (toasts): a step above every
    // surface it may land on, so it never blends into the page.
    QColor surfaceRaised;
    // Status colors, for what went well / wrong (toasts) — kept apart from
    // accent, which is the brand color, not an alarm.
    QColor success;
    QColor danger;
};

// Reads QGuiApplication::styleHints()->colorScheme(); Unknown (no portal/
// desktop integration reporting a scheme) defaults to Light — same
// "unknown defaults to the more common case" convention Integration::TrayIcon
// already uses for its own light/dark glyph pick.
Mode currentMode();
// The user's pick over the desktop's scheme; nullopt follows the desktop.
// A change emits Notifier::changed(), as a desktop switch does.
void setModeOverride(std::optional<Mode> mode);

const Palette& palette(Mode mode);
inline const Palette& palette() { return palette(currentMode()); }

// Glass: the window's chrome (sidebar, track list, toolbar) and its
// popups (menus, tooltips, the track card) painted partly transparent over
// a blur of what's behind the window (Integration::WindowGlass). Set at
// startup, before any window exists, and again from Settings — a change
// emits Notifier::glassChanged() (and changed(), so everything repaints).
void setGlassEnabled(bool enabled);
bool glassEnabled();
// Whether glass is on when the user hasn't chosen: only in a dark scheme —
// a light one's pale tint over a blurred desktop reads washed out — and
// only where the desktop blurs on request (Integration::WindowGlass).
bool glassByDefault();
// Whether the user wants glass: their choice (Config::Settings::
// glassBackground()), or glassByDefault() if they never made one.
inline bool glassWanted(std::optional<bool> userChoice) { return userChoice.value_or(glassByDefault()); }
// How much of our own color covers the blur by default: enough that text
// stays readable over any wallpaper, little enough that it reads as glass.
// `color` as a glass surface: tinted with it, partly see-through — or
// `color` itself, opaque, when glass is off. `opacity`: how much of the
// color covers the blur.
constexpr qreal kGlassOpacity = 0.8;
// The window's chrome — sidebar, toolbar — shaded more, closer to the
// opaque color it stands in for.
constexpr qreal kChromeGlassOpacity = 0.85;
QColor glass(const QColor& color, qreal opacity = kGlassOpacity);
// With glass, the content area's list is a light layer of surface0 at this
// opacity over the chrome glass, so it still reads a step darker.
constexpr qreal kContentGlassLayer = 0.3;
// `top` composited over `bottom` (source-over), both possibly see-through:
// the one color a stack of translucent layers shows as.
QColor over(const QColor& top, const QColor& bottom);

// Fires once, app-wide, whenever the desktop's light/dark scheme changes —
// every theme-driven consumer (the generated stylesheet, the icon tint
// cache, custom-painted delegates) connects to this single signal instead
// of each duplicating its own QStyleHints::colorSchemeChanged hookup.
class Notifier : public QObject {
    Q_OBJECT

public:
    static Notifier& instance();

signals:
    void changed();
    // setGlassEnabled() switched glass on or off — a top-level window has
    // to recreate its native window for the change (translucency is fixed
    // when that's created).
    void glassChanged();

private:
    Notifier();
};

inline Notifier& notifier() { return Notifier::instance(); }

} // namespace Theme
