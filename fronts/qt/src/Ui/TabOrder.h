#pragma once

class QWidget;

namespace Ui {

// Makes Tab walk `root`'s focusable descendants in layout order (left to
// right, top to bottom as the layouts place them), instead of the order the
// widgets happened to be created in — which is what Qt chains by default and
// which rarely matches what is drawn. Returns the last widget of the chain,
// or nullptr if there was none, so the caller can continue it elsewhere.
// Call again when widgets are added later.
QWidget* chainTabOrder(QWidget* root);

// As above, but also chained after `previous` (when not null).
QWidget* chainTabOrderAfter(QWidget* previous, QWidget* root);

} // namespace Ui
