#include "OverlayScrollBar.h"

#include <QAbstractScrollArea>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QVariantAnimation>

#include "Tokens.h"

namespace Ui {

namespace {

constexpr int kAnimationMs = 150;
constexpr qreal kNarrowWidth = 5.0;
constexpr qreal kWideWidth = 9.0;
constexpr qreal kNarrowOpacity = 0.55;
constexpr qreal kWideOpacity = 0.9;
constexpr int kFlashTimeoutMs = 900;
constexpr qreal kMinHandleLength = 24.0;
constexpr int kMargin = 2; // gap between the handle and the viewport's right edge

// Floating handle painted directly on a viewport, entirely replacing the
// real (hidden) QScrollBar's visuals — see OverlayScrollBar's class doc
// for the three-state design this implements. Not Q_OBJECT: it only ever
// connects *to* other objects' signals via lambdas, never declares its
// own, so no moc processing is needed.
class Handle : public QWidget {
public:
    explicit Handle(QAbstractScrollArea* area)
        // Parented to `area` itself, NOT area->viewport(): QAbstractItemView
        // scrolls its viewport via QWidget::scroll(dx, dy) for efficiency
        // (a fast blit + repaint of just the newly-exposed strip, rather
        // than a full repaint), and QWidget::scroll() explicitly also
        // repositions any *child widgets* of the scrolled widget by that
        // same delta — not just the painted pixels. A handle parented to
        // the viewport would get dragged along with every scroll instead
        // of staying pinned in place. `area` itself never scrolls, so a
        // child of it is unaffected; geometry is computed from
        // area->viewport()->geometry(), which is already expressed in
        // area's own coordinate space.
        : QWidget(area)
        , area_(area)
        , bar_(area->verticalScrollBar())
    {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);
        area_->viewport()->installEventFilter(this);

        widthAnim_ = new QVariantAnimation(this);
        widthAnim_->setDuration(kAnimationMs);
        widthAnim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(widthAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            width_ = v.toReal();
            updateHandleRect();
        });

        opacityAnim_ = new QVariantAnimation(this);
        opacityAnim_->setDuration(kAnimationMs);
        opacityAnim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(opacityAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            opacity_ = v.toReal();
            update();
        });

        flashTimer_.setSingleShot(true);
        flashTimer_.setInterval(kFlashTimeoutMs);
        connect(&flashTimer_, &QTimer::timeout, this, [this]() {
            if (!viewHovered_ && !handleHovered_ && !dragging_)
                setState(State::Hidden);
        });

        // Flashes narrow-visible on any scroll (wheel, drag-elsewhere,
        // programmatic) even without a hovering cursor — standard overlay-
        // scrollbar courtesy (macOS/GNOME both do this) so the user gets
        // positional feedback without having to mouse over the view first.
        connect(bar_, &QScrollBar::valueChanged, this, [this](int) {
            updateHandleRect();
            if (!viewHovered_ && !handleHovered_ && !dragging_) {
                setState(State::Narrow);
                flashTimer_.start();
            }
        });
        connect(bar_, &QScrollBar::rangeChanged, this, [this](int, int) { updateHandleRect(); });

        reposition();
        raise();
        show();
    }

    void reposition()
    {
        // area->viewport()->geometry() is already expressed in area's own
        // coordinate space (viewport is area's child) — no manual mapping
        // needed even though this widget is no longer parented to the
        // viewport itself.
        const QRect vp = area_->viewport()->geometry();
        const int x = vp.x() + vp.width() - qRound(kWideWidth) - kMargin;
        setGeometry(x, vp.y(), qRound(kWideWidth), vp.height());
        updateHandleRect();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != area_->viewport())
            return QWidget::eventFilter(watched, event);
        switch (event->type()) {
            case QEvent::Enter:
                setViewHovered(true);
                break;
            case QEvent::Leave:
                setViewHovered(false);
                break;
            case QEvent::Resize:
                reposition();
                break;
            default:
                break;
        }
        return false; // observe only — never block the view's own handling
    }

    void paintEvent(QPaintEvent*) override
    {
        if (opacity_ <= 0.0)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const Theme::Palette& pal = Theme::palette();
        const bool engaged = state_ == State::Wide || dragging_;

        if (engaged) {
            // Full-height shaded backdrop behind the handle, same animated
            // width, right-aligned — the "background under it shades"
            // effect, and the GNOME/macOS "trough" that appears while
            // actively scrolling/dragging.
            QColor track = pal.ink;
            track.setAlphaF(0.12);
            painter.setBrush(track);
            painter.drawRoundedRect(QRectF(width() - width_, 0, width_, height()), width_ / 2.0, width_ / 2.0);
        }

        QColor handleColor = engaged ? pal.inkSecondary : pal.borderStrong;
        handleColor.setAlphaF(opacity_);
        painter.setBrush(handleColor);
        painter.drawRoundedRect(handleRect_, width_ / 2.0, width_ / 2.0);
    }

    void enterEvent(QEnterEvent*) override
    {
        handleHovered_ = true;
        flashTimer_.stop();
        setState(State::Wide);
    }

    void leaveEvent(QEvent*) override
    {
        handleHovered_ = false;
        if (!dragging_)
            setState(viewHovered_ ? State::Narrow : State::Hidden);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !handleRect_.contains(event->position()))
            return;
        dragging_ = true;
        dragStartY_ = event->position().y();
        dragStartValue_ = bar_->value();
        setState(State::Wide);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_)
            return;
        const int range = bar_->maximum() - bar_->minimum();
        if (range <= 0)
            return;
        const qreal scrollableTrack = qMax<qreal>(1.0, height() - handleRect_.height());
        const qreal deltaY = event->position().y() - dragStartY_;
        const int newValue = dragStartValue_ + qRound(deltaY * range / scrollableTrack);
        bar_->setValue(qBound(bar_->minimum(), newValue, bar_->maximum()));
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !dragging_)
            return;
        dragging_ = false;
        setState(handleHovered_ ? State::Wide : (viewHovered_ ? State::Narrow : State::Hidden));
    }

private:
    enum class State { Hidden, Narrow, Wide };

    void setViewHovered(bool hovered)
    {
        viewHovered_ = hovered;
        if (dragging_ || handleHovered_)
            return; // already at or above Narrow — don't downgrade
        setState(hovered ? State::Narrow : State::Hidden);
    }

    void updateHandleRect()
    {
        const qreal trackHeight = height();
        const int range = bar_->maximum() - bar_->minimum();
        qreal handleHeight = trackHeight;
        if (range > 0) {
            const int pageStep = bar_->pageStep() > 0 ? bar_->pageStep() : qRound(trackHeight);
            handleHeight = qMax(kMinHandleLength, trackHeight * pageStep / (range + pageStep));
        }
        qreal y = 0;
        if (range > 0)
            y = (trackHeight - handleHeight) * (bar_->value() - bar_->minimum()) / range;
        handleRect_ = QRectF(width() - width_, y, width_, handleHeight);
        update();
    }

    void setState(State state)
    {
        if (state_ == state)
            return;
        state_ = state;
        qreal targetWidth = 0;
        qreal targetOpacity = 0;
        switch (state) {
            case State::Hidden:
                break;
            case State::Narrow:
                targetWidth = kNarrowWidth;
                targetOpacity = kNarrowOpacity;
                break;
            case State::Wide:
                targetWidth = kWideWidth;
                targetOpacity = kWideOpacity;
                break;
        }
        widthAnim_->stop();
        widthAnim_->setStartValue(width_);
        widthAnim_->setEndValue(targetWidth);
        widthAnim_->start();
        opacityAnim_->stop();
        opacityAnim_->setStartValue(opacity_);
        opacityAnim_->setEndValue(targetOpacity);
        opacityAnim_->start();
    }

    QAbstractScrollArea* area_;
    QScrollBar* bar_;
    QVariantAnimation* widthAnim_;
    QVariantAnimation* opacityAnim_;
    QTimer flashTimer_;
    State state_ = State::Hidden;
    qreal width_ = 0;
    qreal opacity_ = 0;
    bool viewHovered_ = false;
    bool handleHovered_ = false;
    bool dragging_ = false;
    qreal dragStartY_ = 0;
    int dragStartValue_ = 0;
    QRectF handleRect_;
};

} // namespace

void OverlayScrollBar::attach(QAbstractScrollArea* area)
{
    new OverlayScrollBar(area); // parented to area; destroyed along with it
}

OverlayScrollBar::OverlayScrollBar(QAbstractScrollArea* area)
    : QObject(area)
    , area_(area)
{
    // The real scrollbar keeps working (value()/setValue()/valueChanged all
    // still function) — ScrollBarAlwaysOff only stops Qt from showing it or
    // reserving layout space for it, which is exactly the "0px by default"
    // this class replaces with its own floating, non-layout-participating
    // handle.
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    handle_ = new Handle(area);
}

} // namespace Ui
