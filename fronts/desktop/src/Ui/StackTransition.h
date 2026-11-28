#pragma once

#include <QObject>
#include <QPixmap>
#include <QPointer>

#include <functional>
#include <memory>

#include "Transition.h"

class QStackedWidget;

namespace Ui {

// Animated switching between a QStackedWidget's pages, as tabs do it: the
// page left slides out and fades to the side away from the one picked, which
// slides in and fades from the other — a page further on the right comes from
// the right. As AnimatedPresenter does, it animates snapshots of the pages, not
// the live widgets: meanwhile the stack is hidden (keeping its room in the
// layout) and a canvas over it paints them.
class StackTransition : public QObject {
    Q_OBJECT

public:
    explicit StackTransition(QStackedWidget* stack);
    ~StackTransition() override;

    // Shows page `index`. `fit` runs once it's the current page, before its
    // snapshot is taken: whatever fits the window to it. It says the height the
    // window is heading for (-1: it stays as it is) — the window may take its
    // time getting there (it moves meanwhile: see syncCanvas()), and the page
    // is snapshot at the size it'll have then. Without `animate` it's just set,
    // as the first page of a window not shown yet.
    void switchTo(int index, const std::function<int()>& fit, bool animate = true);
    bool isAnimating() const;
    // The stack has moved or been resized (the window was, a step at a time):
    // the canvas goes with it. Not from the stack's own events: hidden, as it
    // is while the snapshots move, it gets none.
    void syncCanvas();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPixmap snapshot(QWidget* page, const QSize& size) const;
    void settle();

    QStackedWidget* stack_;
    QPointer<QWidget> canvas_;
    std::unique_ptr<LayerTransition<QPixmap>> layers_;
};

} // namespace Ui
