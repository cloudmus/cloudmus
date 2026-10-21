#include "Autostart.h"

namespace Integration::Autostart {

// Not on macOS yet: a login item belongs to the app bundle (SMAppService),
// which comes with the packaging. Settings hides the checkbox meanwhile.

bool isEnabled() { return false; }

bool setEnabled(bool) { return false; }

void refresh() { }

} // namespace Integration::Autostart
