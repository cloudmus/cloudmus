#pragma once

#include <QObject>

class QAbstractScrollArea;
class QWidget;

namespace Ui {

// macOS/GNOME-style overlay scrollbars, vertical and horizontal, for any
// QAbstractScrollArea (QTreeView, QListView, QScrollArea, ...): the real
// QScrollBars are hidden entirely (Qt::ScrollBarAlwaysOff — they still work
// programmatically, they're just never shown or given layout space), and a small floating widget is
// painted directly on top of the viewport instead. Three animated states:
// hidden (0 width, the default — reserves no layout space, ever), narrow +
// translucent while the cursor is anywhere over the view, and thicker +
// near-opaque (with a shaded track behind it) while the cursor is over the
// handle itself or dragging it. Also flashes briefly on programmatic
// scrolling (wheel, drag) even without a hovering cursor.
//
// One call attaches it for the lifetime of the target widget:
//   Ui::OverlayScrollBar::attach(someView);
class OverlayScrollBar : public QObject {
    Q_OBJECT

public:
    static void attach(QAbstractScrollArea* area);

private:
    explicit OverlayScrollBar(QAbstractScrollArea* area);

    // All the actual behavior (hover tracking, animation, painting,
    // dragging) lives in a private QWidget subclass defined entirely in
    // the .cpp — this class is just the attach()-facing setup facade
    // (hides the real scrollbar, constructs the handle), so its concrete
    // type stays opaque here.
    QAbstractScrollArea* area_;
    QWidget* verticalHandle_ = nullptr;
    QWidget* horizontalHandle_ = nullptr;
};

} // namespace Ui
