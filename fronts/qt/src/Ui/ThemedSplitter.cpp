#include "ThemedSplitter.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitterHandle>

#include "Tokens.h"

namespace Ui {

namespace {
constexpr int kHandleWidth = 1; // real reserved width == the painted line's width, exactly
constexpr int kGrabMargin = 2; // extra px, on each side of a handle, that still grabs it
} // namespace

// Paints a 1px themed line spanning the (equally thin) handle rect — no
// padding to worry about; see ThemedSplitter's class doc for why the
// wider grab area lives elsewhere instead of here.
class ThemedSplitterHandle : public QSplitterHandle {
public:
    ThemedSplitterHandle(Qt::Orientation orientation, QSplitter* parent)
        : QSplitterHandle(orientation, parent)
    {
    }

protected:
    void paintEvent(QPaintEvent*) override { QPainter(this).fillRect(rect(), Theme::palette().border); }

    // Without this, the handle's actual widget geometry stays at the
    // *style's* PM_SplitterWidth pixel metric (e.g. 5px on Fusion) even
    // after QSplitter::setHandleWidth(1) — confirmed empirically:
    // splitter->handleWidth() correctly reports 1, but the real handle
    // widget still gets laid out at the style's wider default, since
    // QSplitter's layout takes the *larger* of the explicit handleWidth
    // and the handle's own sizeHint(), and the base QSplitterHandle's
    // sizeHint() comes from that same pixel metric. Overriding it
    // directly makes the requested width actually win.
    QSize sizeHint() const override
    {
        return orientation() == Qt::Horizontal ? QSize(splitter()->handleWidth(), 1)
                                                : QSize(1, splitter()->handleWidth());
    }
};

ThemedSplitter::ThemedSplitter(QWidget* parent)
    : QSplitter(parent)
{
    init();
}

ThemedSplitter::ThemedSplitter(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent)
{
    init();
}

ThemedSplitter::~ThemedSplitter()
{
    const bool wasFilterOwner = !instances().isEmpty() && instances().constFirst() == this;
    instances().removeOne(this);
    // Qt automatically drops `this`'s own installEventFilter registration
    // on destruction — if `this` was the instance actually acting as the
    // installed filter, hand that role to whichever instance is left.
    if (wasFilterOwner && !instances().isEmpty())
        QCoreApplication::instance()->installEventFilter(instances().constFirst());
}

QList<ThemedSplitter*>& ThemedSplitter::instances()
{
    static QList<ThemedSplitter*> list;
    return list;
}

QSplitterHandle* ThemedSplitter::anyNeighboringHandle(const QPoint& globalPos)
{
    for (ThemedSplitter* splitter : instances()) {
        if (QSplitterHandle* h = splitter->neighboringHandle(globalPos))
            return h;
    }
    return nullptr;
}

void ThemedSplitter::init()
{
    setHandleWidth(kHandleWidth);
    // Global, not installed on individual panes: a pane can be an
    // arbitrary widget tree (e.g. trackListPane_ is a plain QWidget
    // wrapping a QListView that fills it edge-to-edge via a zero-margin
    // layout) — whichever *specific* descendant actually receives a
    // given mouse event depends on exactly what's laid out at that
    // pixel, not on which widget was added to this splitter. A filter on
    // one specific widget (the pane itself, or even just its immediate
    // viewport for a scroll area) can silently miss events some other
    // descendant captures first — confirmed in practice: it left the
    // grab zone working on only one side of a handle, not symmetric on
    // both, depending on which pane happened to have a event-capturing
    // child covering its edge. Checking global screen-coordinate
    // proximity instead sidesteps this entirely: it doesn't matter which
    // widget actually got the event, only where the cursor is.
    //
    // Only the first-ever instance actually installs the filter; every
    // instance registers itself in the static list either way, and
    // anyNeighboringHandle() checks all of them regardless of which
    // instance's filter is the one actually being called by Qt — see
    // that method and eventFilter()'s own comment for why checking only
    // `this` isn't enough once more than one ThemedSplitter exists.
    if (instances().isEmpty())
        QCoreApplication::instance()->installEventFilter(this);
    instances().append(this);
}

QSplitterHandle* ThemedSplitter::createHandle()
{
    return new ThemedSplitterHandle(orientation(), this);
}

QSplitterHandle* ThemedSplitter::neighboringHandle(const QPoint& globalPos) const
{
    const bool isHorizontal = orientation() == Qt::Horizontal;
    for (int i = 1; i < count(); ++i) {
        QSplitterHandle* h = handle(i);
        if (!h || !h->isVisible())
            continue;
        QRect grabRect(h->mapToGlobal(QPoint(0, 0)), h->size());
        if (isHorizontal)
            grabRect.adjust(-kGrabMargin, 0, kGrabMargin, 0);
        else
            grabRect.adjust(0, -kGrabMargin, 0, kGrabMargin);
        if (grabRect.contains(globalPos))
            return h;
    }
    return nullptr;
}

bool ThemedSplitter::eventFilter(QObject* watchedObject, QEvent* event)
{
    auto* watched = qobject_cast<QWidget*>(watchedObject);

    // Since this filter is global (installed on QCoreApplication, not on
    // individual panes — see the class doc for why), it's invoked for
    // *every* widget's events application-wide, not just ones relevant
    // to this particular splitter instance. Only ever delegate to
    // QSplitter's own base eventFilter() for objects this exact splitter
    // actually manages (one of its own handles) — that base
    // implementation is designed for the "installed on my own children"
    // case, and isn't guaranteed to be a harmless no-op for arbitrary
    // unrelated widgets. Confirmed empirically: without this guard, a
    // second ThemedSplitter instance's blind fallthrough call (returning
    // QSplitter::eventFilter() for literally anything unhandled)
    // interfered with this one ever detecting a nearby handle at all
    // when both were active at the same time — one splitter worked fine
    // alone, but adding a second broke the first's detection entirely.
    if (watched && qobject_cast<QSplitterHandle*>(watched)) {
        if (watched->parentWidget() == this)
            return QSplitter::eventFilter(watchedObject, event);
        return false; // some other splitter's handle, not mine
    }
    if (!watched)
        return false;

    switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
            break;
        default:
            return false;
    }

    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    const QPoint globalPos = mouseEvent->globalPosition().toPoint();

    const auto forwardTo = [globalPos](QSplitterHandle* target, QMouseEvent* source) {
        QMouseEvent forwarded(source->type(), target->mapFromGlobal(globalPos), source->globalPosition(),
            source->button(), source->buttons(), source->modifiers());
        QCoreApplication::sendEvent(target, &forwarded);
    };

    switch (event->type()) {
        case QEvent::MouseButtonPress:
            if (mouseEvent->button() != Qt::LeftButton)
                return false;
            if (QSplitterHandle* target = anyNeighboringHandle(globalPos)) {
                activeDragTarget_ = target;
                forwardTo(target, mouseEvent);
                return true;
            }
            return false;
        case QEvent::MouseMove:
            if (activeDragTarget_) {
                forwardTo(activeDragTarget_, mouseEvent);
                return true;
            }
            if (QSplitterHandle* target = anyNeighboringHandle(globalPos))
                watched->setCursor(target->cursor());
            else
                watched->unsetCursor();
            return false;
        case QEvent::MouseButtonRelease:
            if (activeDragTarget_) {
                forwardTo(activeDragTarget_, mouseEvent);
                activeDragTarget_ = nullptr;
                watched->unsetCursor();
                return true;
            }
            return false;
        default:
            return false;
    }
}

} // namespace Ui
