#pragma once

#include <QModelIndex>

#include <functional>

class QAbstractItemView;

namespace Ui {

// Enter, Return or Space on the view's current row runs `activate` with it and
// the key (Return for Enter) — the keyboard's "default action" (play it, open
// it), the counterpart of the double click these views use. The key is consumed, so the view's own
// handling of it (Space toggling the selection) doesn't also run. Owned by
// the view.
void activateCurrentOnKey(QAbstractItemView* view, std::function<void(const QModelIndex&, Qt::Key)> activate);

} // namespace Ui
