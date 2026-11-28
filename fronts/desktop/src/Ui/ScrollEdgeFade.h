#pragma once

#include <QColor>
#include <QObject>

#include <functional>

class QAbstractScrollArea;
class QWidget;

namespace Ui {

// Soft gradient fades at the top and bottom edge of a scrollable view, in
// the view's own background color, hinting that there's more content
// beyond that edge: the top fade only while scrolled away from the top,
// the bottom one only while not at the bottom. Each fades in/out smoothly
// as that changes.
//
// One call attaches it for the lifetime of the view:
//   Ui::ScrollEdgeFade::attach(view, [] { return Theme::palette().surface0; });
// The color is asked for at paint time, so a theme change is picked up.
//
// Call it BEFORE OverlayScrollBar::attach() on the same view: both float
// over the viewport as children of the area, and the later one ends up on
// top — the scrollbar's handle has to stay above the fades.
class ScrollEdgeFade : public QObject {
    Q_OBJECT

public:
    static void attach(QAbstractScrollArea* area, std::function<QColor()> background);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    ScrollEdgeFade(QAbstractScrollArea* area, std::function<QColor()> background);

    void reposition();
    void refresh();

    QAbstractScrollArea* area_;
    QWidget* top_ = nullptr;
    QWidget* bottom_ = nullptr;
};

} // namespace Ui
