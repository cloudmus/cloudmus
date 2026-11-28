#include "TabOrder.h"

#include <QLayout>
#include <QScrollArea>
#include <QStackedWidget>
#include <QWidget>

namespace Ui {

namespace {

// Hidden and disabled widgets stay in the chain — Qt skips them while
// tabbing — so one that is shown or enabled later (a sign-in button, a track
// control once a track loads) is already in its place.
bool tabs(const QWidget* w) { return w->focusPolicy() & Qt::TabFocus; }

void collect(QWidget* widget, QList<QWidget*>& out);

// What is inside `widget`, not the widget itself.
void collectInside(QWidget* widget, QList<QWidget*>& out);

void collectLayout(QLayout* layout, QList<QWidget*>& out)
{
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (QWidget* child = item->widget())
            collect(child, out);
        else if (QLayout* nested = item->layout())
            collectLayout(nested, out);
    }
}

void collect(QWidget* widget, QList<QWidget*>& out)
{
    if (tabs(widget))
        out.append(widget);
    collectInside(widget, out);
}

void collectInside(QWidget* widget, QList<QWidget*>& out)
{
    if (auto* area = qobject_cast<QScrollArea*>(widget)) {
        if (area->widget() != nullptr)
            collect(area->widget(), out);
        return;
    }
    if (auto* stack = qobject_cast<QStackedWidget*>(widget)) {
        for (int i = 0; i < stack->count(); ++i)
            collect(stack->widget(i), out);
        return;
    }
    if (widget->layout() != nullptr)
        collectLayout(widget->layout(), out);
}

} // namespace

QWidget* chainTabOrder(QWidget* root) { return chainTabOrderAfter(nullptr, root); }

QWidget* chainTabOrderAfter(QWidget* previous, QWidget* root)
{
    QList<QWidget*> widgets;
    if (previous != nullptr)
        widgets.append(previous);
    const int first = widgets.size();
    collectInside(root, widgets);
    if (widgets.size() == first)
        return previous;
    for (int i = 1; i < widgets.size(); ++i)
        QWidget::setTabOrder(widgets[i - 1], widgets[i]);
    return widgets.last();
}

} // namespace Ui
