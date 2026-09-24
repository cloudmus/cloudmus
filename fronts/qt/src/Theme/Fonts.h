#pragma once

namespace Theme {

// Registers the four Manrope weight files (Regular/Medium/SemiBold/Bold —
// see resources/fonts.qrc) via QFontDatabase::addApplicationFont(). Call
// once, early in main(), before anything calls Theme::Typography::font():
// Qt resolves and caches a family's available faces the first time a QFont
// naming it is used, so registering late risks a stale
// substitution sticking around for that first caller.
void registerApplicationFonts();

} // namespace Theme
