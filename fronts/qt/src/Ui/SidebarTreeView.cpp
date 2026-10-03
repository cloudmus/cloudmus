#include "SidebarTreeView.h"

#include <QItemSelectionModel>
#include <QPainter>

#include "NavItemDelegate.h"
#include "SidebarModel.h"
#include "Tokens.h"

namespace Ui {

void SidebarTreeView::drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto kind = static_cast<ViewModel::SidebarModel::Kind>(index.data(ViewModel::SidebarModel::KindRole).toInt());
    if (kind != ViewModel::SidebarModel::Kind::PlaylistsHeader) {
        const auto* delegate = qobject_cast<const NavItemDelegate*>(itemDelegate());
        const QRect band(0, option.rect.top(), viewport()->width(), option.rect.height());
        // No antialiasing: a plain axis-aligned rect, whose soft edges at a
        // fractional display scale would seam between adjacent rows.
        const QColor chrome = Theme::palette().surface100;
        if (selectionModel() != nullptr && selectionModel()->isSelected(index))
            painter->fillRect(band, Theme::selectedFill(chrome));
        else if (delegate != nullptr && delegate->hoveredIndex() == index)
            painter->fillRect(band, Theme::hoverFill(chrome));
    }
    QTreeView::drawRow(painter, option, index);
}

} // namespace Ui
