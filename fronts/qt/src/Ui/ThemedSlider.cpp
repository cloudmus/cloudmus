#include "ThemedSlider.h"

#include <QEnterEvent>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QVariantAnimation>

#include "Metrics.h"
#include "Tokens.h"

namespace Ui {

namespace {

constexpr int kAnimationMs = 150;
constexpr qreal kGrooveHeight = 3.0;
// Vertical room for the full-size handle, so it never gets clipped.
constexpr int kHeight = 16;

QColor mix(const QColor& from, const QColor& to, qreal t)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
        from.greenF() + (to.greenF() - from.greenF()) * t, from.blueF() + (to.blueF() - from.blueF()) * t,
        from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

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

    const qreal handleX = QRectF(handleRect).center().x();
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
