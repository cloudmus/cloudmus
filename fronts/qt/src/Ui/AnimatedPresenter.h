#pragma once

#include <QObject>
#include <QPixmap>
#include <QPointer>

#include <memory>

#include "Transition.h"

class QWidget;

namespace Ui {

// Animated show/hide for an overlay widget (a sheet, a panel, ...) built
// on LayerTransition: instead of animating the live widget tree — a
// QGraphicsOpacityEffect over a list view, a line edit and an overlay
// scrollbar re-renders that whole subtree offscreen every frame and
// misbehaves with nested effects — it animates a snapshot of it. During
// the animation the real widget stays hidden and a lightweight canvas at
// the same geometry paints the snapshot with the effect's opacity/offset/
// scale; once settled the real widget is shown again (or stays hidden).
//
// Interrupting is smooth both ways: presenting mid-dismiss (or the
// reverse) animates from wherever the leaving snapshot currently is — see
// LayerTransition's exit-effect semantics.
//
// The caller owns the target's geometry (e.g. keeps it filling its parent
// on resize); present()/dismiss() just take whatever geometry it has.
class AnimatedPresenter : public QObject {
    Q_OBJECT

public:
    AnimatedPresenter(QWidget* target, TransitionEffect enter, TransitionEffect exit);
    ~AnimatedPresenter() override;

    void present();
    void dismiss();
    // The logical state: true from present() on, even while still animating in.
    bool isPresented() const { return presented_; }

signals:
    // Emitted once the enter/exit animation has fully settled.
    void presented();
    void dismissed();

private:
    QPixmap snapshot() const;
    void settle();

    QWidget* target_;
    // QPointer: the canvas is the target's sibling, so the shared parent
    // may delete it first — raise() reorders siblings, so either can come
    // first in its child list.
    QPointer<QWidget> canvas_;
    bool presented_ = false;
    std::unique_ptr<LayerTransition<QPixmap>> layers_;
};

} // namespace Ui
