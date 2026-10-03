#include "FocusRing.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFocusEvent>
#include <QPainter>

#include "Metrics.h"
#include "Tokens.h"

namespace Theme {

namespace {

bool g_keyboardMode = false;

void repaintFocused()
{
    if (QWidget* focused = QApplication::focusWidget())
        focused->update();
}

void setKeyboardMode(bool on)
{
    if (g_keyboardMode == on)
        return;
    g_keyboardMode = on;
    repaintFocused();
}

} // namespace

FocusRing::FocusRing(QObject* parent)
    : QObject(parent)
{
    qApp->installEventFilter(this);
}

bool FocusRing::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
        case QEvent::FocusIn: {
            if (!watched->isWidgetType())
                break;
            switch (static_cast<QFocusEvent*>(event)->reason()) {
                case Qt::TabFocusReason:
                case Qt::BacktabFocusReason:
                case Qt::ShortcutFocusReason:
                    setKeyboardMode(true);
                    break;
                case Qt::MouseFocusReason:
                    setKeyboardMode(false);
                    break;
                default: // window activation, popup closed: keep the mode
                    break;
            }
            break;
        }
        case QEvent::MouseButtonPress:
            setKeyboardMode(false);
            break;
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}

bool keyboardFocusActive() { return g_keyboardMode; }

bool focusVisible(const QWidget* widget)
{
    if (!g_keyboardMode || widget == nullptr)
        return false;
    const QWidget* window = widget->window();
    const auto* dialog = qobject_cast<const QDialog*>(window);
    return dialog == nullptr || dialog->property("themed").toBool();
}

void paintFocusRing(QPainter* painter, const QRectF& shape, qreal radius, bool onAccent)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(onAccent ? palette().onAccent : palette().accent, Metrics::focusRingWidth));
    const qreal half = Metrics::focusRingWidth / 2.0;
    painter->drawRoundedRect(shape.adjusted(half, half, -half, -half), radius, radius);
    painter->restore();
}

} // namespace Theme
