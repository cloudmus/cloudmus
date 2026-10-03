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

// Ui::ThemedSlider's round handle. Also its PM_SliderLength (see
// CloudMusStyle::pixelMetric()), so the handle QSlider hit-tests is
// exactly the circle that gets painted.
constexpr int sliderHandleDiameter = 12;

// Keyboard focus ring (Theme::paintFocusRing()). Drawn inside the widget's
// own bounds: a ring outside them would be clipped by tight parents (the
// transport bar) and by neighbouring buttons.
constexpr int focusRingWidth = 2;

} // namespace Theme::Metrics
