#pragma once

class QAbstractButton;

namespace Ui {

// Gives a QDialogButtonBox's standard button the app's look: `variant`
// ("primary"/"secondary", see Theme::StyleSheet's buttonsBlock()), the
// button font, and no platform-injected icon.
void styleDialogButton(QAbstractButton* button, const char* variant);

} // namespace Ui
