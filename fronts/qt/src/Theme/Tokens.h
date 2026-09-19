#pragma once

#include <QColor>
#include <QObject>

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
};

// Reads QGuiApplication::styleHints()->colorScheme(); Unknown (no portal/
// desktop integration reporting a scheme) defaults to Light — same
// "unknown defaults to the more common case" convention Integration::TrayIcon
// already uses for its own light/dark glyph pick.
Mode currentMode();

const Palette& palette(Mode mode);
inline const Palette& palette() { return palette(currentMode()); }

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

private:
    Notifier();
};

inline Notifier& notifier() { return Notifier::instance(); }

} // namespace Theme
