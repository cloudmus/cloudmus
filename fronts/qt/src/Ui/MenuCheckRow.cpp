#include "MenuCheckRow.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>

#include "Radius.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"

namespace Ui {

MenuCheckRow::MenuCheckRow(const QString& text, QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_Hover);
    setMouseTracking(true);
    box_ = new QCheckBox(text, this);
    box_->setFont(Theme::font(Theme::TextStyle::Button));
    // The row takes the clicks (see mouseReleaseEvent) so the whole width
    // toggles, not just the box and its label.
    box_->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(
        Theme::Spacing::space2, Theme::Spacing::space1, Theme::Spacing::space3, Theme::Spacing::space1);
    layout->addWidget(box_);
}

void MenuCheckRow::paintEvent(QPaintEvent*)
{
    if (!underMouse() || !isEnabled() || !box_->isEnabled())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::palette().surface400);
    painter.drawRoundedRect(QRectF(rect()), Theme::Radius::sm, Theme::Radius::sm);
}

void MenuCheckRow::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    update();
}

void MenuCheckRow::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    update();
}

void MenuCheckRow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && box_->isEnabled())
        box_->toggle();
}

} // namespace Ui
