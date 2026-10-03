#pragma once

#include <QTreeView>

namespace Ui {

// The sidebar tree. Paints each hovered/selected row's highlight itself, once,
// across the whole viewport width — indentation and chevron strip included —
// so the band is a single translucent fill: pieces painted separately by Qt
// (the strip left of the item) and by the delegate would overlap and double
// the alpha under glass, or leave a seam where they meet.
class SidebarTreeView : public QTreeView {
    Q_OBJECT

public:
    using QTreeView::QTreeView;

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace Ui
