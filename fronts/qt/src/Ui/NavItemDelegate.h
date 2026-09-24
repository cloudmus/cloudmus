#pragma once

#include <QPersistentModelIndex>
#include <QStyledItemDelegate>

namespace Ui {

// Paints the sidebar tree's rows per the CloudMus design system's NavItem
// spec: hover/selected backgrounds and accent-colored selected text, plus
// the non-interactive "Playlists" section header in label-upper style. A
// custom delegate — rather than QSS ::item rules — because SidebarModel's
// "Playlists" header needs different treatment (no hover, no selection
// background at all) from every other row in the same QTreeView, which
// plain ::item/::item:selected selectors can't distinguish (see
// SidebarModel::Kind).
//
// Hover is tracked here ourselves (setHoveredIndex()), not read from
// option.state's State_MouseOver: Qt's item views only recompute "which
// row is hovered" from QHoverEvent, delivered by Qt's own event dispatch
// on real pointer motion — dragging a scrollbar slides row content under
// a stationary cursor without ever producing one, so State_MouseOver keeps
// pointing at whatever row was last under the cursor before the scroll.
// Owning hover state ourselves lets MainWindow recompute and repaint it
// correctly on scroll too (via QAbstractItemView::indexAt(), not by trying
// to spoof the event Qt would have delivered).
class NavItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    void setHoveredIndex(const QModelIndex& index) { hoveredIndex_ = index; }
    QModelIndex hoveredIndex() const { return hoveredIndex_; }

private:
    QPersistentModelIndex hoveredIndex_;
};

} // namespace Ui
