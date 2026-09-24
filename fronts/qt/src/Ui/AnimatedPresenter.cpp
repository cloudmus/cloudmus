#include "AnimatedPresenter.h"

#include <QLayout>
#include <QPainter>
#include <QWidget>

#include <functional>

namespace Ui {

namespace {

// Paints the presenter's snapshot layers; only visible while animating.
// Swallows mouse input meanwhile (a plain QWidget accepts nothing, but
// being on top keeps clicks from reaching whatever is underneath), so a
// click can't land on a half-transparent sheet mid-animation.
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

AnimatedPresenter::AnimatedPresenter(QWidget* target, TransitionEffect enter, TransitionEffect exit)
    : QObject(target)
    , target_(target)
{
    canvas_ = new Canvas(target->parentWidget(), [this](QPainter& painter) {
        const QPointF center = QRectF(canvas_->rect()).center();
        layers_->forEach([&](const QPixmap& pixmap, const LayerState& state) {
            painter.save();
            applyLayerState(painter, state, center);
            painter.drawPixmap(0, 0, pixmap);
            painter.restore();
        });
    });
    layers_ = std::make_unique<LayerTransition<QPixmap>>(canvas_, std::move(enter), std::move(exit));
    layers_->setOnSettled([this]() { settle(); });
    target_->hide();
}

// The canvas is the target's sibling, not owned by this object — delete it
// explicitly (unless the parent already has) so it can never paint through
// a dangling `this`.
AnimatedPresenter::~AnimatedPresenter() { delete canvas_.data(); }

QPixmap AnimatedPresenter::snapshot() const
{
    // A hidden widget can still be grabbed, but its layout only runs on
    // show — activate it first so the snapshot shows the final layout,
    // not whatever stale geometry the children had before.
    target_->ensurePolished();
    if (target_->layout() != nullptr)
        target_->layout()->activate();
    return target_->grab();
}

void AnimatedPresenter::present()
{
    if (presented_)
        return;
    presented_ = true;
    const QPixmap shot = snapshot();
    canvas_->setGeometry(target_->geometry());
    canvas_->show();
    canvas_->raise();
    layers_->show(shot);
}

void AnimatedPresenter::dismiss()
{
    if (!presented_)
        return;
    presented_ = false;
    // Still entering: its snapshot is already the current layer. Fully
    // presented: the live widget may have changed since (filter typed,
    // list scrolled) — refresh the layer from it before it leaves.
    if (target_->isVisible()) {
        if (QPixmap* current = layers_->current())
            *current = snapshot();
        else
            layers_->show(snapshot(), /*animate=*/false);
        target_->hide();
    }
    canvas_->setGeometry(target_->geometry());
    canvas_->show();
    canvas_->raise();
    layers_->hide();
}

void AnimatedPresenter::settle()
{
    canvas_->hide();
    if (presented_) {
        target_->show();
        target_->raise();
        emit presented();
    } else {
        emit dismissed();
    }
}

} // namespace Ui
