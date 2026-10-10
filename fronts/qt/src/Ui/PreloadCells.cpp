#include "PreloadCells.h"

#include <QAbstractItemModel>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>

#include "FrameClock.h"
#include "Metrics.h"
#include "Preload.h"
#include "Spacing.h"
#include "Tokens.h"

namespace Ui {

namespace {

// A full-size cell and the gap between two.
constexpr int kCellWidth = Theme::Spacing::space3;
constexpr int kCellGap = Theme::Spacing::space1 - 1;
// As wide as this many full-size cells, and no wider.
constexpr int kMaxFullCells = 6;
constexpr int kMaxWidth = kMaxFullCells * kCellWidth + (kMaxFullCells - 1) * kCellGap;
// Thinner cells keep a hairline between them, down to this width.
constexpr qreal kTickGap = 1.0;
constexpr qreal kMinTickWidth = 3.0;
// The same room the seek slider takes, so both sit on one line.
constexpr int kHeight = Theme::Metrics::sliderHandleDiameter + Theme::Spacing::space1;
// Share of the remaining distance covered per frame at 60 Hz.
constexpr qreal kEasePerFrame = 0.2;

} // namespace

PreloadCells::PreloadCells(ViewModel::Preload& preload, QWidget* parent)
    : QWidget(parent)
    , preload_(preload)
    , ticker_(new FrameTicker(this))
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMouseTracking(true);
    connect(ticker_, &FrameTicker::frame, this, &PreloadCells::onFrame);
    connect(&preload_, &ViewModel::Preload::progressChanged, this, &PreloadCells::reload);
    connect(&preload_, &ViewModel::Preload::progressModeChanged, this, &PreloadCells::reload);
    reload();
    // Nothing downloaded ahead: no room taken either.
    setVisible(!target_.isEmpty());
}

PreloadCells::Layout PreloadCells::layout() const
{
    const int count = int(target_.size());
    if (count <= kMaxFullCells)
        return Layout::Cells;
    return (kMaxWidth - (count - 1) * kTickGap) / count >= kMinTickWidth ? Layout::Ticks : Layout::Bar;
}

qreal PreloadCells::cellGap() const { return layout() == Layout::Cells ? kCellGap : kTickGap; }

qreal PreloadCells::cellWidth() const
{
    const int count = int(target_.size());
    if (layout() == Layout::Cells || count == 0)
        return kCellWidth;
    return (kMaxWidth - (count - 1) * kTickGap) / count;
}

QSize PreloadCells::sizeHint() const
{
    const int count = int(target_.size());
    if (count == 0)
        return { 0, kHeight };
    const int width = layout() == Layout::Cells ? count * kCellWidth + (count - 1) * kCellGap : kMaxWidth;
    return { width, kHeight };
}

void PreloadCells::reload()
{
    const QAbstractItemModel* model = preload_.upcoming();
    const int count = model->rowCount();
    const bool resized = count != target_.size();
    target_.resize(count);
    for (int row = 0; row < count; ++row)
        target_[row] = model->data(model->index(row, 0), ViewModel::Preload::FractionRole).toDouble();
    // Other tracks than before: nothing to ease from.
    const bool smooth = !resized && preload_.progressMode() == ViewModel::Preload::ProgressMode::Smooth && isVisible();
    if (smooth) {
        if (ticker_->state() != QAbstractAnimation::Running)
            ticker_->start();
    } else {
        ticker_->stop();
        shown_ = target_;
        shownOverall_ = preload_.overallFraction();
    }
    if (resized) {
        setVisible(count > 0);
        updateGeometry();
    }
    updateToolTip();
    update();
}

void PreloadCells::onFrame()
{
    // Eases toward the reported progress, and stops once it's there: no
    // frames are painted while nothing moves.
    bool moving = false;
    const auto ease = [&moving](qreal& value, qreal target) {
        if (target < value || std::abs(target - value) < 0.005) {
            value = target; // a restart isn't worth animating backwards
            return;
        }
        value += (target - value) * kEasePerFrame;
        moving = true;
    };
    for (qsizetype i = 0; i < shown_.size(); ++i)
        ease(shown_[i], target_.at(i));
    ease(shownOverall_, preload_.overallFraction());
    if (!moving)
        ticker_->stop();
    update();
}

void PreloadCells::paintEvent(QPaintEvent*)
{
    if (target_.isEmpty())
        return;
    const Theme::Palette& pal = Theme::palette();
    QColor fill = pal.accent;
    fill.setAlphaF(0.35); // what ThemedSlider marks the downloaded part with

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const qreal height = Theme::Metrics::sliderGrooveHeight;
    const qreal top = rect().center().y() + 0.5 - height / 2.0;
    const qreal radius = height / 2.0;

    const auto cell = [&](qreal left, qreal width, qreal fraction) {
        painter.setBrush(pal.border);
        painter.drawRoundedRect(QRectF(left, top, width, height), radius, radius);
        if (fraction <= 0)
            return;
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(left, top, width * qMin(1.0, fraction), height), radius, radius);
    };

    if (layout() == Layout::Bar) {
        cell(0, width(), shownOverall_);
        return;
    }
    const qreal step = cellWidth() + cellGap();
    for (qsizetype i = 0; i < shown_.size(); ++i)
        cell(i * step, cellWidth(), shown_.at(i));
}

int PreloadCells::cellAt(qreal x) const
{
    if (layout() != Layout::Cells || target_.isEmpty())
        return -1;
    const int index = int(x / (cellWidth() + cellGap()));
    return index >= 0 && index < target_.size() ? index : -1;
}

void PreloadCells::updateToolTip()
{
    // One by one while each track has a cell of its own to point at; all
    // of them together otherwise.
    if (hovered_ >= 0 && hovered_ < target_.size() && layout() == Layout::Cells) {
        const QAbstractItemModel* model = preload_.upcoming();
        setToolTip(model->data(model->index(hovered_, 0), ViewModel::Preload::DescriptionRole).toString());
    } else {
        setToolTip(preload_.summaryText());
    }
}

void PreloadCells::mouseMoveEvent(QMouseEvent* event)
{
    const int index = cellAt(event->position().x());
    if (index != hovered_) {
        hovered_ = index;
        updateToolTip();
    }
    QWidget::mouseMoveEvent(event);
}

void PreloadCells::leaveEvent(QEvent* event)
{
    hovered_ = -1;
    updateToolTip();
    QWidget::leaveEvent(event);
}

} // namespace Ui
