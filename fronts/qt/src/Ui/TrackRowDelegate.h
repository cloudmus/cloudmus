#pragma once

#include <QStyledItemDelegate>

namespace Ui {

class CoverArtCache;

// Compact, one-line row: small inline cover thumbnail + title/artist
// stacked + duration right-aligned — see the plan's UI/UX design (chosen
// over a multi-column table). Pulls colors from the option's palette at
// paint time so it follows KDE light/dark theme switches automatically.
class TrackRowDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit TrackRowDelegate(CoverArtCache* coverCache, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

signals:
    // The cover thumbnail's hover-only play overlay (see paint()) was
    // clicked. MainWindow wires this to the same handler as a row
    // double-click — both mean "play this one track".
    void playRequested(const QModelIndex& index);

private:
    // Shared between paint() (drawing the hover overlay) and editorEvent()
    // (hit-testing a click against it) so they can never disagree.
    QRect thumbRect(const QRect& rowRect) const;

    CoverArtCache* coverCache_;
    static constexpr int kRowHeight = 48;
    static constexpr int kThumbSize = 36;
};

} // namespace Ui
