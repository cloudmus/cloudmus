#pragma once

#include <QObject>

class QAbstractScrollArea;
class QPropertyAnimation;

namespace Ui {

// Animates mouse-wheel scrolling on any QAbstractScrollArea (QTreeView,
// QListView, QScrollArea, ...) instead of Qt's default instant per-notch
// jump. Each wheel notch still covers the same distance Qt would normally
// scroll (computed by handing the event to the scrollbar itself, so this
// never has to guess/duplicate Qt's own step-size formula) — only the
// motion is eased instead of instantaneous. Scrolling again before the
// current animation finishes extends it to the new total distance rather
// than restarting from (or snapping back to) the live, still-mid-flight
// position.
//
// One call attaches it for the lifetime of the target widget:
//   Ui::SmoothScroller::attach(someView);
class SmoothScroller : public QObject {
    Q_OBJECT

public:
    static void attach(QAbstractScrollArea* area);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit SmoothScroller(QAbstractScrollArea* area);

    QAbstractScrollArea* area_;
    QPropertyAnimation* animation_ = nullptr;
    int target_ = 0;
};

} // namespace Ui
