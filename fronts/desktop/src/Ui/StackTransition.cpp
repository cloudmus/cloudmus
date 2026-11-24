#include "StackTransition.h"

#include <QEvent>
#include <QLayout>
#include <QPainter>
#include <QStackedWidget>

namespace Ui {

namespace {

// How far a page slides as it comes and goes, and how long that takes. The
// one left is gone at once, fading fastest at the start: anything slower
// trails over the one coming in.
constexpr qreal kShift = 32;
TransitionEffect enterFrom(int direction)
{
    return { 260, QEasingCurve::OutCubic, { 0.0, 1.0, QPointF(kShift * direction, 0) } };
}
TransitionEffect exitTo(int direction)
{
    return { 70, QEasingCurve::OutQuad, { 0.0, 1.0, QPointF(-kShift * direction, 0) } };
}

// Paints the pages' snapshots while they move; hidden otherwise. Takes the
// clicks meanwhile, for none to land on a page on its way.
class Canvas : public QWidget {
public:
    Canvas(QWidget* parent, std::function<void(QPainter&)> paint)
        : QWidget(parent)
        , paint_(std::move(paint))
    {
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        paint_(painter);
    }
    void mousePressEvent(QMouseEvent*) override { }
    void mouseReleaseEvent(QMouseEvent*) override { }
    void mouseDoubleClickEvent(QMouseEvent*) override { }

private:
    std::function<void(QPainter&)> paint_;
};

} // namespace

StackTransition::StackTransition(QStackedWidget* stack)
    : QObject(stack)
    , stack_(stack)
{
    // Hidden while the snapshots move, it keeps its room: nothing around it
    // jumps.
    QSizePolicy policy = stack_->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    stack_->setSizePolicy(policy);

    canvas_ = new Canvas(stack_->parentWidget(), [this](QPainter& painter) {
        // From the top left: the page left may be taller than the room the
        // one picked has, and is cut there.
        const QPointF center = QRectF(canvas_->rect()).center();
        layers_->forEach([&](const QPixmap& pixmap, const LayerState& state) {
            painter.save();
            applyLayerState(painter, state, center);
            painter.drawPixmap(0, 0, pixmap);
            painter.restore();
        });
    });
    layers_ = std::make_unique<LayerTransition<QPixmap>>(canvas_, enterFrom(1), exitTo(1));
    layers_->setOnSettled([this]() { settle(); });
    stack_->installEventFilter(this);
}

// The canvas is the stack's sibling, not owned by this object.
StackTransition::~StackTransition() { delete canvas_.data(); }

bool StackTransition::isAnimating() const { return canvas_ && canvas_->isVisible(); }

QPixmap StackTransition::snapshot(QWidget* page, const QSize& size) const
{
    // At `size`, the room the page is to have: its layout is made to it first,
    // and the stack's own layout puts the page back as it was.
    const QRect before = page->geometry();
    page->ensurePolished();
    page->setGeometry(QRect(before.topLeft(), size));
    if (page->layout() != nullptr)
        page->layout()->activate();
    // Without the window's background (grab() paints it): what's behind the
    // pages (the About dialog's clouds) shows through as it does around them.
    const qreal ratio = page->devicePixelRatioF();
    QPixmap pixmap(page->size() * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    page->render(&pixmap, QPoint(), QRegion(), QWidget::DrawChildren);
    page->setGeometry(before);
    if (page->layout() != nullptr)
        page->layout()->activate();
    return pixmap;
}

void StackTransition::switchTo(int index, const std::function<int()>& fit, bool animate)
{
    const int from = stack_->currentIndex();
    if (index == from || index < 0 || index >= stack_->count()) {
        if (fit)
            fit();
        return;
    }
    // Not shown yet: nothing to see move. (The stack itself is hidden while
    // it moves, so it's the window that's asked.)
    if (!animate || !stack_->window()->isVisible()) {
        stack_->setCurrentIndex(index);
        if (fit)
            fit();
        return;
    }
    // Moving already: the current layer is the page last picked, as it was.
    if (!isAnimating())
        layers_->show(snapshot(stack_->currentWidget(), stack_->size()), /*animate=*/false);
    const int direction = index > from ? 1 : -1;
    layers_->setEffects(enterFrom(direction), exitTo(direction));

    stack_->setCurrentIndex(index);
    const int windowHeight = stack_->window()->height();
    const int target = fit ? fit() : -1;
    // The stack gets what the window gets: the rest of the dialog is as tall as ever.
    const int stackHeight = target < 0 ? stack_->height() : stack_->height() + target - windowHeight;
    const QPixmap picked = snapshot(stack_->currentWidget(), QSize(stack_->width(), stackHeight));

    stack_->hide();
    canvas_->setGeometry(stack_->geometry());
    canvas_->show();
    canvas_->raise();
    layers_->show(picked);
}

bool StackTransition::eventFilter(QObject* watched, QEvent* event)
{
    // The window fitted to the page picked: the canvas goes with the stack.
    if (watched == stack_ && canvas_ && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        syncCanvas();
    return QObject::eventFilter(watched, event);
}

void StackTransition::syncCanvas()
{
    if (canvas_ && canvas_->isVisible())
        canvas_->setGeometry(stack_->geometry());
}

void StackTransition::settle()
{
    canvas_->hide();
    stack_->show();
}

} // namespace Ui
