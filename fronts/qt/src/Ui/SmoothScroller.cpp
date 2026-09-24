#include "SmoothScroller.h"

#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>

namespace Ui {

namespace {
constexpr int kAnimationMs = 180;
} // namespace

void SmoothScroller::attach(QAbstractScrollArea* area)
{
    new SmoothScroller(area); // parented to area; destroyed along with it
}

SmoothScroller::SmoothScroller(QAbstractScrollArea* area)
    : QObject(area)
    , area_(area)
{
    area_->viewport()->installEventFilter(this);
}

bool SmoothScroller::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == area_->viewport() && event->type() == QEvent::Wheel) {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        if (wheelEvent->angleDelta().y() == 0)
            return false; // horizontal-only wheel input: leave to default handling

        QScrollBar* bar = area_->verticalScrollBar();
        if (!bar || bar->minimum() == bar->maximum())
            return false;

        // Hand the event to the scrollbar itself to find out exactly how far
        // Qt's own wheelEvent() would have jumped it — reusing that logic
        // instead of reimplementing angleDelta()/singleStep()/
        // wheelScrollLines() math by hand, which would drift from whatever
        // this Qt version actually does. Then undo the instant jump and
        // animate to it instead.
        //
        // Signals blocked for the whole probe, not just the undo: the
        // area follows valueChanged, and a QScrollArea would otherwise move
        // its content to the jump target right away and stay there — the
        // visible "jump ahead, snap back, then glide" — until the animation
        // next changes the value.
        const int before = bar->value();
        int stepDelta = 0;
        {
            const QSignalBlocker blocker(bar);
            QCoreApplication::sendEvent(bar, wheelEvent);
            stepDelta = bar->value() - before;
            bar->setValue(before);
        }

        const bool animating = animation_ && animation_->state() == QAbstractAnimation::Running;
        const int base = animating ? target_ : before;
        target_ = qBound(bar->minimum(), base + stepDelta, bar->maximum());

        if (!animation_) {
            animation_ = new QPropertyAnimation(bar, "value", this);
            animation_->setEasingCurve(QEasingCurve::OutCubic);
        }
        animation_->stop();
        animation_->setDuration(kAnimationMs);
        animation_->setStartValue(bar->value());
        animation_->setEndValue(target_);
        animation_->start();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace Ui
