#include "FirstFrameBackground.h"

#include <QColor>
#include <QWidget>

#include <windows.h>

#include "Tokens.h"

namespace Integration {

bool FirstFrameBackground::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const auto* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_ERASEBKGND || Theme::glassEnabled())
        return false;

    const QWidget* widget = QWidget::find(reinterpret_cast<WId>(msg->hwnd));
    if (widget == nullptr || !widget->isWindow())
        return false;
    const Qt::WindowType type = widget->windowType();
    if (type != Qt::Window && type != Qt::Dialog)
        return false;

    const QColor color = Theme::palette().surface0;
    HBRUSH brush = CreateSolidBrush(RGB(color.red(), color.green(), color.blue()));
    RECT rect;
    GetClientRect(msg->hwnd, &rect);
    FillRect(reinterpret_cast<HDC>(msg->wParam), &rect, brush);
    DeleteObject(brush);
    if (result)
        *result = 1;
    return true;
}

} // namespace Integration
