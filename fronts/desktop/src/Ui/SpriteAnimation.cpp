#include "SpriteAnimation.h"

#include <QPainter>

namespace Ui {

SpriteAnimation::SpriteAnimation(
    const QString& strip, int frameCount, int frameMs, QSize size, bool pixelArt, QWidget* parent)
    : QWidget(parent)
    , strip_(strip)
    , frameCount_(qMax(1, frameCount))
    , size_(size)
    , pixelArt_(pixelArt)
{
    // A picture of a set size, not something a layout should stretch.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    timer_.setInterval(frameMs);
    connect(&timer_, &QTimer::timeout, this, [this]() {
        frame_ = (frame_ + 1) % frameCount_;
        update();
    });
}

void SpriteAnimation::paintEvent(QPaintEvent*)
{
    if (strip_.isNull())
        return;
    const int frameWidth = strip_.width() / frameCount_;
    const QRect source(frame_ * frameWidth, 0, frameWidth, strip_.height());
    // Kept to the frame's proportions, centered.
    const QSize drawn = source.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - drawn.width()) / 2, (height() - drawn.height()) / 2), drawn);
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, !pixelArt_);
    painter.drawPixmap(target, strip_, source);
}

void SpriteAnimation::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    timer_.start();
}

void SpriteAnimation::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    timer_.stop();
}

} // namespace Ui
