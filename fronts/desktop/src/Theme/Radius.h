#pragma once

// The CloudMus design system's corner radii.

namespace Theme::Radius {

constexpr int sm = 4; // buttons, inputs, badges, slider handle
constexpr int md = 8; // panels, popups
// 1.5x radius-sm — cover-art thumbnails in track rows only, per the design
// system's TrackRow spec (the tile is bigger than a control, but not a full
// panel either).
constexpr int coverArtSm = 6;
constexpr int full = 999; // circular: play button, slider handle

} // namespace Theme::Radius
