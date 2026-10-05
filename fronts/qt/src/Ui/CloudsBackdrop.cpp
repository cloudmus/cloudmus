#include "CloudsBackdrop.h"

#include <QCursor>
#include <QPainter>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

#include "AudioPulse.h"
#include "Icons.h"
#include "Tokens.h"

namespace Ui {

namespace {

constexpr int kCloudCount = 14;
constexpr int kFrameMs = 16;
constexpr qreal kNearestDepth = 0.15; // past this a cloud has left the frame
constexpr qreal kDriftPerSecond = 0.12; // of the depth range: ~7 s to fly past
constexpr qreal kRushBoost = 5.0; // extra speed, in multiples of the drift, at a full beat
constexpr qreal kRushFallMs = 400;
constexpr qreal kFlashFallMs = 300;
constexpr qreal kMaxOpacity = 0.055; // "barely visible"
constexpr qreal kLevelOpacity = 0.7; // how much loudness adds on top
constexpr qreal kLevelSwell = 0.12;
// A beat swells the clouds and lights them up for a moment, on top.
constexpr qreal kBeatSwell = 0.15;
constexpr qreal kBeatOpacity = 1.0;
constexpr qreal kLeanShare = 0.04; // of the widget's size, at the edge
constexpr qreal kLeanFollowMs = 250;
constexpr int kSpritePixels = 384;
// The logo's gradient, lit: its glow behind the logo.
const QColor kGlowColor(0xff, 0x9a, 0x5c);

qreal rand01() { return QRandomGenerator::global()->generateDouble(); }

} // namespace

CloudsBackdrop::CloudsBackdrop(ViewModel::AudioPulse& pulse, QWidget* origin, QWidget* parent)
    : QWidget(parent)
    , pulse_(pulse)
    , origin_(origin)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    clouds_.resize(kCloudCount);
    for (Cloud& cloud : clouds_)
        respawn(cloud, true);

    Theme::followTheme(this, [this]() { rebuildSprite(); });

    timer_.setInterval(kFrameMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &CloudsBackdrop::tick);

    connect(&pulse_, &ViewModel::AudioPulse::beat, this, [this](float strength) {
        rush_ = std::max(rush_, qreal(strength));
        flash_ = std::max(flash_, qreal(strength));
    });
}

void CloudsBackdrop::rebuildSprite()
{
    // White in a dark theme; in a light one white would vanish, so the
    // brand color.
    const QColor color = Theme::currentMode() == Theme::Mode::Dark ? QColor(Qt::white) : Theme::palette().accent;
    const qreal dpr = devicePixelRatioF();
    QPixmap sprite(QSize(kSpritePixels, kSpritePixels * 5 / 8) * dpr);
    sprite.setDevicePixelRatio(dpr);
    sprite.fill(Qt::transparent);
    {
        QPainter painter(&sprite);
        painter.setRenderHint(QPainter::Antialiasing);
        QSvgRenderer(QStringLiteral(":/icons/icons/cloud.svg"))
            .render(&painter, QRectF(QPointF(), sprite.deviceIndependentSize()));
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(sprite.rect(), color);
    }
    sprite_ = sprite;
    update();
}

void CloudsBackdrop::respawn(Cloud& cloud, bool anywhere)
{
    // Not straight at the logo, so a cloud's path doesn't hide behind it.
    const qreal angle = rand01() * 2 * M_PI;
    const qreal radius = 0.25 + rand01() * 0.75;
    cloud.direction = { std::cos(angle) * radius, std::sin(angle) * radius };
    cloud.depth = anywhere ? kNearestDepth + rand01() * (1 - kNearestDepth) : 1.0;
    cloud.flipped = rand01() < 0.5;
}

QPointF CloudsBackdrop::vanishingPoint() const
{
    QPointF center = rect().center();
    if (origin_)
        center = QPointF(origin_->mapTo(parentWidget(), origin_->rect().center()));
    return center + QPointF(lean_.x() * width(), lean_.y() * height()) * kLeanShare;
}

void CloudsBackdrop::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    pulse_.acquire();
    clock_.start();
    timer_.start();
}

void CloudsBackdrop::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    timer_.stop();
    pulse_.release();
}

void CloudsBackdrop::tick()
{
    const qreal dtMs = clock_.restart();
    const qreal dt = dtMs / 1000;
    time_ += dt;
    rush_ *= std::exp(-dtMs / kRushFallMs);
    flash_ *= std::exp(-dtMs / kFlashFallMs);

    const qreal speed = kDriftPerSecond * (1 + kRushBoost * rush_);
    for (Cloud& cloud : clouds_) {
        cloud.depth -= speed * dt;
        if (cloud.depth < kNearestDepth)
            respawn(cloud, false);
    }

    // The mouse: where it sits relative to the widget's center, as a share
    // of half its size.
    const QPointF cursor = mapFromGlobal(QCursor::pos());
    const QPointF half(width() / 2.0, height() / 2.0);
    const QPointF target(std::clamp((cursor.x() - half.x()) / half.x(), -1.0, 1.0),
        std::clamp((cursor.y() - half.y()) / half.y(), -1.0, 1.0));
    lean_ += (target - lean_) * (1 - std::exp(-dtMs / kLeanFollowMs));

    update();
}

void CloudsBackdrop::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPointF vanish = vanishingPoint();
    const qreal level = pulse_.level();

    // The glow behind the logo: breathes while it's quiet, follows the
    // loudness, flashes on a hit.
    {
        const qreal breath = 0.5 + 0.5 * std::sin(time_ * 1.3);
        const qreal alpha = std::min(1.0, 0.07 + 0.04 * breath + 0.3 * level + 0.35 * flash_);
        const qreal radius = std::min(width(), height()) * (0.28 + 0.14 * level + 0.10 * flash_);
        QRadialGradient glow(vanish, radius);
        QColor inner = kGlowColor;
        inner.setAlphaF(alpha);
        QColor outer = kGlowColor;
        outer.setAlphaF(0);
        glow.setColorAt(0, inner);
        glow.setColorAt(1, outer);
        painter.fillRect(rect(), glow);
    }

    // Far ones first, so a near one covers them.
    QVector<const Cloud*> order;
    for (const Cloud& cloud : clouds_)
        order.append(&cloud);
    std::sort(order.begin(), order.end(), [](const Cloud* a, const Cloud* b) { return a->depth > b->depth; });

    const qreal half = std::max(width(), height()) / 2.0;
    const qreal base = std::min(width(), height()) * 0.5;
    for (const Cloud* cloud : order) {
        const qreal nearness = 1 / cloud->depth - 1; // 0 at the vanishing point, growing as it comes
        const QPointF center = vanish + cloud->direction * half * nearness * 0.35;
        const qreal w = base / cloud->depth * 0.3 * (1 + kLevelSwell * level + kBeatSwell * flash_);
        const qreal h = w * sprite_.deviceIndependentSize().height() / sprite_.deviceIndependentSize().width();

        // In from the distance, out as it passes.
        const qreal fadeIn = std::clamp((1 - cloud->depth) / 0.15, 0.0, 1.0);
        const qreal fadeOut = std::clamp((cloud->depth - kNearestDepth) / 0.15, 0.0, 1.0);
        const qreal opacity = kMaxOpacity * (1 + kLevelOpacity * level + kBeatOpacity * flash_) * fadeIn * fadeOut;
        if (opacity < 0.002)
            continue;

        painter.setOpacity(opacity);
        const QRectF target(center.x() - w / 2, center.y() - h / 2, w, h);
        if (cloud->flipped) {
            painter.save();
            painter.translate(center);
            painter.scale(-1, 1);
            painter.drawPixmap(QRectF(-w / 2, -h / 2, w, h), sprite_, QRectF(sprite_.rect()));
            painter.restore();
        } else {
            painter.drawPixmap(target, sprite_, QRectF(sprite_.rect()));
        }
    }
}

} // namespace Ui
