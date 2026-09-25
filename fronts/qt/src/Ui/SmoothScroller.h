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
// The returned instance is only needed for programmatic scrolling
// (scrollTo()), which shares the wheel's animation so the two never fight.
class SmoothScroller : public QObject {
    Q_OBJECT

public:
    static SmoothScroller* attach(QAbstractScrollArea* area);

    // Glides to `value` (clamped to the scrollbar's range), the way a jump
    // to a section should look — longer than one wheel notch's glide.
    void scrollTo(int value);

    // Moves the whole motion by `delta` — current position, start and end
    // alike — for content above the viewport that just changed height:
    // keeps what's on screen still, and keeps an in-flight glide heading
    // for the same content rather than for a now-stale pixel offset.
    void shift(int delta);

    bool isAnimating() const;

signals:
    // A glide (wheel or scrollTo()) reached its end.
    void finished();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit SmoothScroller(QAbstractScrollArea* area);

    void animateTo(int target, int durationMs);

    QAbstractScrollArea* area_;
    QPropertyAnimation* animation_ = nullptr;
    int target_ = 0;
};

} // namespace Ui
