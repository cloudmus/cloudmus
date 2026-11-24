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
    switch (msg->message) {
        case WM_SHOWWINDOW:
            // Shown again (from the tray, a dialog reopened): its first
            // frame is due again.
            if (msg->wParam)
                painted_.remove(msg->hwnd);
            return false;
        case WM_PAINT:
            painted_.insert(msg->hwnd);
            return false;
        case WM_NCDESTROY:
            painted_.remove(msg->hwnd);
            return false;
        case WM_ERASEBKGND:
            break;
        default:
            return false;
    }
    // The erase that matters comes from ShowWindow() itself, before any
    // WM_PAINT; the one inside the first paint is skipped, Qt paints over
    // it right away.
    if (painted_.contains(msg->hwnd))
        return false;

    const QWidget* widget = QWidget::find(reinterpret_cast<WId>(msg->hwnd));
    // Per window, not per glass setting: with glass on, the main window is
    // translucent, but dialogs stay opaque and still need their first frame
    // filled.
    if (widget == nullptr || !widget->isWindow() || widget->testAttribute(Qt::WA_TranslucentBackground))
        return false;
    const Qt::WindowType type = widget->windowType();
    if (type != Qt::Window && type != Qt::Dialog)
        return false;

    // What each paints first: the main window's content tone, a dialog's
    // chrome tone (Theme::StyleSheet's dialogsBlock()).
    const QColor color = type == Qt::Dialog ? Theme::palette().surface100 : Theme::palette().surface0;
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
