#include "PulsingLogo.h"

#include <QEasingCurve>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

#include "AudioPulse.h"
#include "LogoSound.h"

namespace Ui {

namespace {

constexpr qreal kBeatSwell = 0.18; // growth at a full-strength beat
constexpr qreal kLevelSwell = 0.05; // growth at full loudness
constexpr int kSettleMs = 380;
// The logo's own sound: it's the logo's, so it swells harder, and settles
// slower, as the handpan rings.
constexpr qreal kStrikeSwell = 0.32;
constexpr qreal kSoundLevelSwell = 0.08;
constexpr int kStrikeSettleMs = 480;

} // namespace

PulsingLogo::PulsingLogo(ViewModel::AudioPulse& pulse, ViewModel::LogoSound& sound, int side, QWidget* parent)
    : QWidget(parent)
    , pulse_(pulse)
    , sound_(sound)
    , renderer_(QStringLiteral(":/icons/icons/logo.svg"))
    , side_(side)
{
    // The room for the largest swell is part of the widget's size: a fixed
    // size is the point here (the layout must not move as the logo grows).
    const qreal largest = std::max(kBeatSwell + kLevelSwell, kStrikeSwell + kSoundLevelSwell);
    const int room = int(side * largest) / 2 + 1;
    setFixedSize(side + 2 * room, side + 2 * room);
    setCursor(Qt::PointingHandCursor);

    for (QVariantAnimation* animation : { &swell_, &strike_ }) {
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
    }
    swell_.setDuration(kSettleMs);
    strike_.setDuration(kStrikeSettleMs);

    const auto kick = [](QVariantAnimation& animation, float strength) {
        animation.stop();
        animation.setStartValue(qreal(strength));
        animation.setEndValue(0.0);
        animation.start();
    };
    connect(&pulse_, &ViewModel::AudioPulse::beat, this, [this, kick](float strength) { kick(swell_, strength); });
    connect(&pulse_, &ViewModel::AudioPulse::levelChanged, this, qOverload<>(&QWidget::update));
    connect(&sound_, &ViewModel::LogoSound::hit, this, [this, kick](float strength) { kick(strike_, strength); });
    connect(&sound_, &ViewModel::LogoSound::levelChanged, this, qOverload<>(&QWidget::update));
}

void PulsingLogo::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        sound_.play();
    QWidget::mousePressEvent(event);
}

void PulsingLogo::paintEvent(QPaintEvent*)
{
    const auto now = [](const QVariantAnimation& animation) {
        return animation.state() == QAbstractAnimation::Running ? animation.currentValue().toReal() : 0.0;
    };
    const qreal scale = 1 + kBeatSwell * now(swell_) + kLevelSwell * pulse_.level() + kStrikeSwell * now(strike_)
        + kSoundLevelSwell * sound_.level();
    const qreal size = side_ * scale;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer_.render(&painter, QRectF((width() - size) / 2, (height() - size) / 2, size, size));
}

} // namespace Ui
