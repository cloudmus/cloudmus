#include "ThemedSlider.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyleOptionSlider>
#include <QVariantAnimation>

#include "Metrics.h"
#include "Radius.h"
#include "Shadow.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"

namespace Ui {

namespace {

constexpr int kAnimationMs = 150;
constexpr qreal kGrooveHeight = 3.0;
// Vertical room for the full-size handle, so it never gets clipped.
constexpr int kHeight = 16;

constexpr int kBubbleAnimationMs = 140;
constexpr int kBubbleGap = 4; // between the bubble's pointer tip and the handle's top
constexpr int kBubblePointer = 6; // pointer triangle height
constexpr int kBubbleShadow = 6;

QColor mix(const QColor& from, const QColor& to, qreal t)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
        from.greenF() + (to.greenF() - from.greenF()) * t, from.blueF() + (to.blueF() - from.blueF()) * t,
        from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

// The value label floating above a dragged handle: a rounded chip with a
// small pointer down at the handle. Pops in (fade + grow from its pointer
// tip + a small rise) on show() and back out on dismiss(); never takes
// mouse input.
class ValueBubble : public QWidget {
public:
    explicit ValueBubble(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        anim_ = new QVariantAnimation(this);
        anim_->setDuration(kBubbleAnimationMs);
        connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            progress_ = v.toReal();
            update();
        });
        connect(anim_, &QVariantAnimation::finished, this, [this]() {
            if (progress_ <= 0.0)
                hide();
        });
        hide();
    }

    // `tip` is where the pointer should point, in parent coordinates.
    void setContent(const QString& text, const QPoint& tip)
    {
        text_ = text;
        const QFontMetrics metrics(font());
        const QSize chip(metrics.horizontalAdvance(text_) + 2 * Theme::Spacing::space2,
            metrics.height() + 2 * Theme::Spacing::space1);
        const QSize full(chip.width() + 2 * kBubbleShadow, chip.height() + kBubblePointer + 2 * kBubbleShadow);
        // Centered over the handle, but kept inside the window near its
        // edges — the pointer then slides along the chip to stay on the handle.
        int x = tip.x() - full.width() / 2;
        if (parentWidget() != nullptr)
            x = qBound(0, x, qMax(0, parentWidget()->width() - full.width()));
        const int minPointerX = kBubbleShadow + Theme::Radius::sm + kBubblePointer;
        pointerX_ = qBound(minPointerX, tip.x() - x, full.width() - minPointerX);
        setGeometry(x, tip.y() - full.height() + kBubbleShadow, full.width(), full.height());
        update();
    }

    void pop()
    {
        show();
        raise();
        animateTo(1.0, QEasingCurve::OutBack);
    }

    void dismiss() { animateTo(0.0, QEasingCurve::InCubic); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (progress_ <= 0.0)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const Theme::Palette& pal = Theme::palette();
        const qreal t = qBound(0.0, progress_, 1.2); // OutBack overshoots slightly past 1

        // Grow from the pointer tip, rising a little as it appears.
        const QPointF tip(pointerX_, height() - kBubbleShadow);
        painter.setOpacity(qMin(1.0, t));
        painter.translate(tip + QPointF(0, (1.0 - t) * 4.0));
        const qreal scale = 0.6 + 0.4 * t;
        painter.scale(scale, scale);
        painter.translate(-tip);

        const QRect chip
            = rect().adjusted(kBubbleShadow, kBubbleShadow, -kBubbleShadow, -kBubbleShadow - kBubblePointer);
        Theme::paintSoftShadow(&painter, chip, kBubbleShadow, 1, 40, Theme::Radius::sm);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(chip), Theme::Radius::sm, Theme::Radius::sm);
        QPolygonF pointer;
        pointer << QPointF(tip.x() - kBubblePointer, chip.bottom() + 0.5)
                << QPointF(tip.x() + kBubblePointer, chip.bottom() + 0.5)
                << QPointF(tip.x(), chip.bottom() + 0.5 + kBubblePointer);
        shape.addPolygon(pointer);
        shape = shape.simplified();
        painter.setPen(QPen(pal.border, 1));
        painter.setBrush(pal.surface400);
        painter.drawPath(shape);

        painter.setFont(font());
        painter.setPen(pal.ink);
        painter.drawText(chip, Qt::AlignCenter, text_);
    }

private:
    void animateTo(qreal target, QEasingCurve::Type curve)
    {
        anim_->stop();
        anim_->setEasingCurve(curve);
        anim_->setStartValue(progress_);
        anim_->setEndValue(target);
        anim_->start();
    }

    QString text_;
    int pointerX_ = 0;
    qreal progress_ = 0.0;
    QVariantAnimation* anim_;
};

} // namespace

ThemedSlider::ThemedSlider(Scheme scheme, HandleVisibility handleVisibility, QWidget* parent)
    : QSlider(Qt::Horizontal, parent)
    , scheme_(scheme)
    , handleVisibility_(handleVisibility)
{
    setAttribute(Qt::WA_Hover, true);

    engagedAnim_ = new QVariantAnimation(this);
    engagedAnim_->setDuration(kAnimationMs);
    engagedAnim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(engagedAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        engagedProgress_ = v.toReal();
        update();
    });

    // Stay engaged while dragging even after the cursor leaves the widget.
    connect(this, &QSlider::sliderPressed, this, &ThemedSlider::refreshEngaged);
    connect(this, &QSlider::sliderReleased, this, &ThemedSlider::refreshEngaged);
    connect(this, &QSlider::sliderPressed, this, &ThemedSlider::showBubble);
    connect(this, &QSlider::sliderMoved, this, &ThemedSlider::updateBubble);
    connect(this, &QSlider::valueChanged, this, [this]() {
        if (isSliderDown())
            updateBubble();
    });
    connect(this, &QSlider::sliderReleased, this, &ThemedSlider::hideBubble);
    connect(&Theme::notifier(), &Theme::Notifier::changed, this, qOverload<>(&QWidget::update));
}

void ThemedSlider::setScheme(Scheme scheme)
{
    scheme_ = scheme;
    update();
}

void ThemedSlider::setHandleVisibility(HandleVisibility handleVisibility)
{
    handleVisibility_ = handleVisibility;
    update();
}

void ThemedSlider::setValueBubble(BubbleFormatter format)
{
    bubbleFormat_ = std::move(format);
    if (!bubbleFormat_)
        hideBubble();
}

QPointF ThemedSlider::handleCenter() const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
    return { QRectF(handleRect).center().x(), rect().center().y() + 0.5 };
}

void ThemedSlider::showBubble()
{
    if (!bubbleFormat_)
        return;
    if (bubble_ == nullptr || bubble_->parentWidget() != window()) {
        delete bubble_.data();
        bubble_ = new ValueBubble(window());
        bubble_->setFont(Theme::tabularFont(Theme::TextStyle::Caption));
    }
    updateBubble();
    static_cast<ValueBubble*>(bubble_.data())->pop();
}

void ThemedSlider::updateBubble()
{
    if (!bubbleFormat_ || bubble_ == nullptr)
        return;
    // sliderPosition(), not value(): with tracking off (or while a seek is
    // pending) value() lags the handle the user is actually dragging.
    const QPointF center = handleCenter();
    const int handleTop = qRound(center.y() - Theme::Metrics::sliderHandleDiameter / 2.0);
    const QPoint tip = mapTo(window(), QPoint(qRound(center.x()), handleTop - kBubbleGap));
    static_cast<ValueBubble*>(bubble_.data())->setContent(bubbleFormat_(sliderPosition()), tip);
}

void ThemedSlider::hideBubble()
{
    if (bubble_ != nullptr)
        static_cast<ValueBubble*>(bubble_.data())->dismiss();
}

QSize ThemedSlider::sizeHint() const { return { QSlider::sizeHint().width(), kHeight }; }

QSize ThemedSlider::minimumSizeHint() const { return { QSlider::minimumSizeHint().width(), kHeight }; }

void ThemedSlider::paintEvent(QPaintEvent*)
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect grooveRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    const Theme::Palette& pal = Theme::palette();
    const qreal t = engagedProgress_;
    const bool accent = scheme_ == Scheme::Accent;
    const QColor groove = mix(pal.border, pal.borderStrong, t);
    const QColor fill = accent ? mix(pal.accent, pal.accentHover, t) : mix(pal.inkSecondary, pal.ink, t);
    const QColor handle = accent ? mix(pal.accent, pal.accentHover, t) : pal.ink;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    const qreal centerY = rect().center().y() + 0.5;
    const qreal radius = kGrooveHeight / 2.0;
    // Inset by half the handle on each side, so the line ends exactly under
    // the handle's center at either extreme instead of poking out past it.
    const qreal inset = handleRect.width() / 2.0;
    const QRectF grooveF(grooveRect.left() + inset, centerY - radius, grooveRect.width() - 2.0 * inset, kGrooveHeight);
    painter.setBrush(groove);
    painter.drawRoundedRect(grooveF, radius, radius);

    if (!isEnabled())
        return;

    const qreal handleX = handleCenter().x();
    if (handleX > grooveF.left()) {
        painter.setBrush(fill);
        painter.drawRoundedRect(
            QRectF(grooveF.left(), grooveF.top(), handleX - grooveF.left(), kGrooveHeight), radius, radius);
    }

    // An OnHover handle grows in from nothing (and fades) as the slider
    // engages; an Always handle is simply there at full size.
    const qreal handleScale = handleVisibility_ == HandleVisibility::Always ? 1.0 : t;
    if (handleScale <= 0.0)
        return;
    QColor handleColor = handle;
    handleColor.setAlphaF(handleColor.alphaF() * handleScale);
    const qreal handleRadius = Theme::Metrics::sliderHandleDiameter / 2.0 * handleScale;
    painter.setBrush(handleColor);
    painter.drawEllipse(QPointF(handleX, centerY), handleRadius, handleRadius);
}

void ThemedSlider::enterEvent(QEnterEvent* event)
{
    QSlider::enterEvent(event);
    refreshEngaged();
}

void ThemedSlider::leaveEvent(QEvent* event)
{
    QSlider::leaveEvent(event);
    refreshEngaged();
}

void ThemedSlider::refreshEngaged()
{
    const qreal target = (underMouse() || isSliderDown()) ? 1.0 : 0.0;
    if (engagedAnim_->endValue().toReal() == target && engagedAnim_->state() == QAbstractAnimation::Running)
        return;
    if (engagedAnim_->state() != QAbstractAnimation::Running && engagedProgress_ == target)
        return;
    engagedAnim_->stop();
    engagedAnim_->setStartValue(engagedProgress_);
    engagedAnim_->setEndValue(target);
    engagedAnim_->start();
}

} // namespace Ui
