#pragma once

#include <QString>

class QApplication;

namespace Theme {

enum class Mode;

// Assembles the app's one global Qt Style Sheet for the given theme mode —
// button variants, sliders, scrollbar, a couple of objectName-targeted
// panels (#sourceAuthCard, #toastLabel), QMenu, QProgressBar, and the
// sidebar tree's branch chevrons. QSS has no variables, so this interpolates
// literal hex values from Tokens::palette(mode) — see applyGlobalStyleSheet()
// for why regenerating this on every theme change is required, not optional.
QString buildStyleSheet(Mode mode);

// Applies buildStyleSheet(Tokens::currentMode()) to `app`, and re-applies it
// automatically every time Tokens::notifier().changed() fires. Call once,
// early in main() (after Fonts::registerApplicationFonts()).
void applyGlobalStyleSheet(QApplication& app);

} // namespace Theme
