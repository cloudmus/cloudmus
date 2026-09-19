#pragma once

namespace Theme::Metrics {

// Icon Button (transport controls, hamburger menu): 36px hit-zone, 20px
// glyph — see NowPlayingBar's IconHoverButton for why this is a step up
// from the design system's base 32/16 spec.
constexpr int iconButtonSize = 36;
constexpr int iconGlyphSize = 20;

// Play Button (HeroPanel): 40px circle, 16px glyph, per the design
// system's Button spec.
constexpr int playButtonSize = 40;
constexpr int playGlyphSize = 16;

} // namespace Theme::Metrics
