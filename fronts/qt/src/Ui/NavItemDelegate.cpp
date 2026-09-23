#include "NavItemDelegate.h"

#include <QIcon>
#include <QPainter>
#include <QWidget>

#include "Icons.h"
#include "SidebarModel.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"

namespace Ui {

void NavItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto kind = static_cast<SidebarModel::Kind>(index.data(SidebarModel::KindRole).toInt());
    const Theme::Palette& pal = Theme::palette();
    const QRect rect = option.rect;

    painter->save();

    if (kind == SidebarModel::Kind::PlaylistsHeader) {
        // Non-selectable group separator (SidebarModel::setSelectable(false))
        // — no hover/selection background regardless of option.state.
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setFont(Theme::font(Theme::TextStyle::LabelUpper));
        painter->setPen(pal.inkTertiary);
        painter->drawText(rect.adjusted(Theme::Spacing::space4, 0, -Theme::Spacing::space4, 0),
            Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
        painter->restore();
        return;
    }

    const bool selected = option.state & QStyle::State_Selected;
    // hoveredIndex_, not option.state & QStyle::State_MouseOver — see the
    // class doc comment on why Qt's own hover tracking can't be trusted
    // across a scroll and MainWindow drives this one directly instead.
    const bool hovered = index == hoveredIndex_;
    // Qt does NOT auto-paint a selection background before calling a fully
    // overridden QStyledItemDelegate::paint() — that's only what
    // QStyledItemDelegate's own *default* paint() does internally, which
    // we've completely replaced. So this delegate has to paint its own
    // background for both states; the QTreeView `selection-background-
    // color`/`selection-color` QSS properties only affect the small native
    // "current item" decoration outside the delegate's own item painting
    // (see Theme::StyleSheet's QTreeView rule), not this fill.
    //
    // Right edge only extends to the viewport's full width — option.rect's
    // own right edge falls short of it (a bare column-width quirk on this
    // single-column, header-hidden tree), which used to leave the highlight
    // looking like a narrow box rather than spanning the row. The left edge
    // stays at rect.left(), NOT 0: that would paint over the branch/chevron
    // decoration to its left, which QTreeView draws separately (and first),
    // so a full-width fill starting at 0 painted right over it.
    const int fullRowWidth = option.widget != nullptr ? option.widget->width() : rect.right();
    const QRect fullRowRect(rect.left(), rect.top(), fullRowWidth - rect.left(), rect.height());
    // No antialiasing for this fill: it's a plain axis-aligned rect, and at
    // a fractional display scale factor AA softens its edges into a
    // partial-opacity blend — which, between two adjacent rows' fills (one
    // freshly hovered, one just cleared), left a faint seam line neither
    // repaint fully overwrote. Enabled below, only for the text/icon.
    if (selected)
        painter->fillRect(fullRowRect, pal.surface400);
    else if (hovered)
        painter->fillRect(fullRowRect, pal.surface300);
    painter->setRenderHint(QPainter::Antialiasing);

    // space2, not space4: rect.left() already includes Qt's own indentation
    // reservation for this row's branch/chevron column (QTreeView::
    // indentation(), space-5 — see MainWindow), so a further space-4 here
    // stacked on top of it, not instead of it, leaving an oversized gap
    // between the chevron and the text specifically on expandable rows
    // (History/Local Folder, with no chevron/children, looked fine either
    // way — nothing to stack with there).
    // Resolved here, at paint time, rather than stored as a QIcon in the
    // model: a tinted icon depends on the current theme and on this row's
    // selection state, neither of which the model knows about.
    constexpr int kIconSide = 16;
    const Theme::IconColor iconColor = selected ? Theme::IconColor::Accent : Theme::IconColor::InkSecondary;
    QIcon rowIcon;
    if (kind == SidebarModel::Kind::History)
        rowIcon = Theme::icon(QStringLiteral("history"), iconColor, kIconSide);
    else if (const QString path = index.data(SidebarModel::SourceIconPathRole).toString(); !path.isEmpty())
        rowIcon = Theme::iconFromFile(path, iconColor, kIconSide);
    else
        rowIcon = index.data(Qt::DecorationRole).value<QIcon>();

    int textLeft = rect.left() + Theme::Spacing::space2;
    if (!rowIcon.isNull()) {
        const QRect iconRect(textLeft, rect.center().y() - kIconSide / 2, kIconSide, kIconSide);
        rowIcon.paint(painter, iconRect);
        textLeft = iconRect.right() + Theme::Spacing::space2;
    }

    // A source header's status sits at the row's right edge, so it never
    // displaces the source's own icon. Auth problem / fetch error win over
    // loading — they're the actionable states (right-click to retry).
    int textRight = rect.right() - Theme::Spacing::space2;
    if (kind == SidebarModel::Kind::SourceHeader) {
        QIcon statusIcon;
        if (index.data(SidebarModel::HasAuthProblemRole).toBool()
            || index.data(SidebarModel::HasFetchErrorRole).toBool())
            statusIcon = Theme::icon(QStringLiteral("warning"), Theme::IconColor::Accent, kIconSide);
        else if (index.data(SidebarModel::IsLoadingRole).toBool())
            statusIcon = Theme::icon(QStringLiteral("refresh"), Theme::IconColor::InkSecondary, kIconSide);
        if (!statusIcon.isNull()) {
            const int statusLeft = fullRowRect.right() - Theme::Spacing::space2 - kIconSide + 1;
            statusIcon.paint(painter, QRect(statusLeft, rect.center().y() - kIconSide / 2, kIconSide, kIconSide));
            textRight = qMin(textRight, statusLeft - Theme::Spacing::space2 - 1);
        }
    }

    QFont textFont = Theme::font(Theme::TextStyle::Body);
    if (selected)
        textFont.setWeight(QFont::DemiBold);
    painter->setFont(textFont);
    painter->setPen(selected ? pal.accent : pal.ink);

    const QRect textRect(textLeft, rect.top(), textRight - textLeft + 1, rect.height());
    const QFontMetrics metrics(textFont);
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
        metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width()));

    painter->restore();
}

QSize NavItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto kind = static_cast<SidebarModel::Kind>(index.data(SidebarModel::KindRole).toInt());
    const int height = kind == SidebarModel::Kind::PlaylistsHeader ? 24 : 32;
    return QSize(option.rect.width(), height);
}

} // namespace Ui
