#include "ScrollEdgeFade.h"

#include <QAbstractScrollArea>
#include <QEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QScrollBar>
#include <QVariantAnimation>

namespace Ui {

namespace {

constexpr int kFadeHeight = 28;
constexpr int kAnimationMs = 180;

// One edge's fade strip: a gradient from the background color (at the
// view's edge) to transparent, times an animated opacity. Parented to the
// scroll area itself, NOT its viewport — see OverlayScrollBar's Handle for
// why a child of the viewport would get dragged along with every scroll.
class FadeStrip : public QWidget {
public:
    FadeStrip(QWidget* parent, bool atTop, std::function<QColor()> background)
        : QWidget(parent)
        , atTop_(atTop)
        , background_(std::move(background))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        anim_ = new QVariantAnimation(this);
        anim_->setDuration(kAnimationMs);
        anim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            opacity_ = v.toReal();
            update();
        });
        show();
    }

    void setShown(bool shown)
    {
        const qreal target = shown ? 1.0 : 0.0;
        if (target == target_)
            return;
        target_ = target;
        anim_->stop();
        anim_->setStartValue(opacity_);
        anim_->setEndValue(target);
        anim_->start();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (opacity_ <= 0.0)
            return;
        const QColor color = background_();
        const auto withAlpha = [&](qreal a) {
            QColor c = color;
            c.setAlphaF(a * opacity_);
            return c;
        };
        QLinearGradient gradient(0, atTop_ ? 0 : height(), 0, atTop_ ? height() : 0);
        gradient.setColorAt(0.0, withAlpha(1.0));
        gradient.setColorAt(0.5, withAlpha(0.55));
        gradient.setColorAt(1.0, withAlpha(0.0));
        QPainter(this).fillRect(rect(), gradient);
    }

private:
    bool atTop_;
    std::function<QColor()> background_;
    QVariantAnimation* anim_;
    qreal opacity_ = 0.0;
    qreal target_ = 0.0;
};

} // namespace

void ScrollEdgeFade::attach(QAbstractScrollArea* area, std::function<QColor()> background)
{
    new ScrollEdgeFade(area, std::move(background));
}

ScrollEdgeFade::ScrollEdgeFade(QAbstractScrollArea* area, std::function<QColor()> background)
    : QObject(area)
    , area_(area)
{
    top_ = new FadeStrip(area, /*atTop=*/true, background);
    bottom_ = new FadeStrip(area, /*atTop=*/false, background);
    area_->viewport()->installEventFilter(this);
    QScrollBar* bar = area_->verticalScrollBar();
    connect(bar, &QScrollBar::valueChanged, this, &ScrollEdgeFade::refresh);
    connect(bar, &QScrollBar::rangeChanged, this, &ScrollEdgeFade::refresh);
    reposition();
    refresh();
}

bool ScrollEdgeFade::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == area_->viewport() && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        reposition();
    return false; // observe only
}

void ScrollEdgeFade::reposition()
{
    // viewport()->geometry() is in the area's own coordinates (it's the
    // area's child), same as these strips.
    const QRect vp = area_->viewport()->geometry();
    const int h = qMin(kFadeHeight, vp.height() / 2);
    top_->setGeometry(vp.left(), vp.top(), vp.width(), h);
    bottom_->setGeometry(vp.left(), vp.bottom() + 1 - h, vp.width(), h);
}

void ScrollEdgeFade::refresh()
{
    const QScrollBar* bar = area_->verticalScrollBar();
    static_cast<FadeStrip*>(top_)->setShown(bar->value() > bar->minimum());
    static_cast<FadeStrip*>(bottom_)->setShown(bar->value() < bar->maximum());
}

} // namespace Ui
