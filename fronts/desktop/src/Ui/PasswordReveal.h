#pragma once

class QLineEdit;

namespace Ui {

// Puts `edit` in password mode with an eye button at its trailing edge
// that shows/hides what's typed — so a pasted password or key can be
// checked before it's saved.
void addPasswordReveal(QLineEdit* edit);

} // namespace Ui
