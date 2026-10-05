#include "PulsingLogo.h"

#include <QEasingCurve>
#include <QPainter>

#include "AudioPulse.h"

namespace Ui {

namespace {

constexpr qreal kBeatSwell = 0.18; // growth at a full-strength beat
constexpr qreal kLevelSwell = 0.05; // growth at full loudness
constexpr int kSettleMs = 380;

} // namespace

PulsingLogo::PulsingLogo(ViewModel::AudioPulse& pulse, int side, QWidget* parent)
    : QWidget(parent)
    , pulse_(pulse)
    , renderer_(QStringLiteral(":/icons/icons/logo.svg"))
    , side_(side)
{
    // The room for the largest swell is part of the widget's size: a fixed
    // size is the point here (the layout must not move as the logo grows).
    const int room = int(side * (kBeatSwell + kLevelSwell)) / 2 + 1;
    setFixedSize(side + 2 * room, side + 2 * room);

    swell_.setEasingCurve(QEasingCurve::OutCubic);
    swell_.setDuration(kSettleMs);
    connect(&swell_, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));

    connect(&pulse_, &ViewModel::AudioPulse::beat, this, [this](float strength) {
        swell_.stop();
        swell_.setStartValue(qreal(strength));
        swell_.setEndValue(0.0);
        swell_.start();
    });
    connect(&pulse_, &ViewModel::AudioPulse::levelChanged, this, qOverload<>(&QWidget::update));
}

void PulsingLogo::paintEvent(QPaintEvent*)
{
    const qreal swell = swell_.state() == QAbstractAnimation::Running ? swell_.currentValue().toReal() : 0;
    const qreal scale = 1 + kBeatSwell * swell + kLevelSwell * pulse_.level();
    const qreal size = side_ * scale;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer_.render(&painter, QRectF((width() - size) / 2, (height() - size) / 2, size, size));
}

} // namespace Ui
