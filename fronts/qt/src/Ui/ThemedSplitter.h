#pragma once

#include <QList>
#include <QSplitter>

namespace Ui {

// A QSplitter whose handle paints as a thin 1px themed line — the real
// reserved layout width matches this exactly, so panes sit truly edge-to-
// edge with no gap of a mismatched/default color to paint over (tried
// that: a wider handle with unpainted padding around a thin line just
// shows Qt's own default background there, visibly wrong against panes
// with dynamic content, e.g. the hero panel's generated per-track
// gradient, that no fixed fill color could blend into).
//
// The wider grab area this still needs comes from a global,
// application-wide event filter (installed on QCoreApplication itself,
// not on individual panes) — see the .cpp for why per-pane filtering
// doesn't work reliably (a pane can be an arbitrary widget tree, and
// whichever specific descendant actually receives a given mouse event
// depends on exactly what's laid out at that pixel).
class ThemedSplitter : public QSplitter {
    Q_OBJECT

public:
    explicit ThemedSplitter(QWidget* parent = nullptr);
    ThemedSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);
    ~ThemedSplitter() override;

protected:
    QSplitterHandle* createHandle() override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void init();
    QSplitterHandle* neighboringHandle(const QPoint& globalPos) const;

    // Every live instance is checked, not just `this`, when deciding
    // whether some handle (belonging to *any* ThemedSplitter) is near a
    // given position — see the .cpp for why: with each instance only
    // ever checking its own handles, whichever instance's globally-
    // installed filter happens to run last (Qt calls multiple filters on
    // the same object in reverse-installation order) could clear a
    // cursor another instance had just correctly set, one splitter
    // stomping on another's decision depending on install order.
    static QList<ThemedSplitter*>& instances();
    static QSplitterHandle* anyNeighboringHandle(const QPoint& globalPos);

    QSplitterHandle* activeDragTarget_ = nullptr;
};

} // namespace Ui
