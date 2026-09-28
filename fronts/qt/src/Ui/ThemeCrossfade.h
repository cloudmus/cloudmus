#pragma once

#include <functional>

namespace Ui {

// Runs `change` — something that restyles the whole app, like switching
// the light/dark scheme — as a crossfade instead of a cut: every visible
// window is snapshotted first, the snapshot laid over it, and once
// `change` has repainted everything underneath, the snapshot fades out.
//
// Only for changes the app makes itself: a desktop scheme switch is
// reported after the fact, too late for a snapshot of the old look.
void crossfadeThemeChange(const std::function<void()>& change);

} // namespace Ui
