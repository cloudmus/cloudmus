#include "FirstPaintCloak.h"

#include <QEvent>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <windows.h>

#include <dwmapi.h>

namespace Integration {

namespace {

// Set while a window waits for its first paint.
constexpr char kCloakedProperty[] = "cloudmusCloaked";
// Uncloaked by then even if no paint came — a window mustn't stay hidden.
constexpr int kMaxCloakMs = 300;

void setCloaked(QWidget* window, bool cloaked)
{
    const BOOL value = cloaked ? TRUE : FALSE;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), DWMWA_CLOAK, &value, sizeof(value));
    window->setProperty(kCloakedProperty, cloaked ? QVariant(true) : QVariant());
}

void uncloakSoon(QWidget* window)
{
    // After the event loop's turn: the paint is only flushed to the
    // window after paintEvent() returns.
    QTimer::singleShot(0, window, [window]() {
        if (window->property(kCloakedProperty).isValid())
            setCloaked(window, false);
    });
}

bool isPlainWindow(const QWidget* widget)
{
    if (!widget->isWindow())
        return false;
    const Qt::WindowType type = widget->windowType();
    return type == Qt::Window || type == Qt::Dialog;
}

} // namespace

bool FirstPaintCloak::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::Show && type != QEvent::Paint)
        return false;
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget == nullptr || !isPlainWindow(widget))
        return false;
    if (type == QEvent::Show) {
        // Sent before Qt shows the native window, so it's cloaked from its
        // very first frame.
        setCloaked(widget, true);
        QTimer::singleShot(kMaxCloakMs, widget, [window = QPointer<QWidget>(widget)]() {
            if (window && window->property(kCloakedProperty).isValid())
                setCloaked(window, false);
        });
    } else if (widget->property(kCloakedProperty).isValid()) {
        uncloakSoon(widget);
    }
    return false;
}

} // namespace Integration
