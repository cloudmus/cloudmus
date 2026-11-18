#include "TrayIcon.h"

#include <QCoreApplication>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QWheelEvent>

#include <windows.h>

#include <shellapi.h>

namespace Integration {

namespace {

// Qt's tray icon window, the same one NotificationToastWin.cpp finds:
// Shell_NotifyIcon is registered on it with uID 0, and a scroll over the
// icon arrives as a system-wide wheel with no event for QSystemTrayIcon.
QSystemTrayIcon* wheelIcon = nullptr;
HHOOK wheelHook = nullptr;
HWND trayHwnd = nullptr;

HWND qtTrayWindow()
{
    if (trayHwnd != nullptr && IsWindow(trayHwnd))
        return trayHwnd;
    trayHwnd = nullptr;
    struct Search {
        DWORD process = GetCurrentProcessId();
        HWND found = nullptr;
    } search;
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* search = reinterpret_cast<Search*>(param);
            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            wchar_t title[32] = { };
            if (process == search->process && GetWindowTextW(hwnd, title, 32) > 0
                && wcscmp(title, L"QTrayIconMessageWindow") == 0) {
                search->found = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    trayHwnd = search.found;
    return trayHwnd;
}

// Posted, not handled inside the hook: a low-level hook that doesn't
// return at once gets dropped by Windows.
void deliverWheel(int delta)
{
    const QPointer<QSystemTrayIcon> icon = wheelIcon;
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [icon, delta]() {
            if (icon == nullptr)
                return;
            QWheelEvent event(QPointF(), QPointF(), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(icon, &event);
        },
        Qt::QueuedConnection);
}

LRESULT CALLBACK wheelProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == WM_MOUSEWHEEL && wheelIcon != nullptr) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        const HWND hwnd = qtTrayWindow();
        if (hwnd != nullptr) {
            NOTIFYICONIDENTIFIER id = { };
            id.cbSize = sizeof(id);
            id.hWnd = hwnd;
            id.uID = 0;
            RECT rect = { };
            // The hook's point and this rect are both physical pixels.
            if (SUCCEEDED(Shell_NotifyIconGetRect(&id, &rect)) && PtInRect(&rect, info->pt)) {
                deliverWheel(GET_WHEEL_DELTA_WPARAM(info->mouseData));
                return 1;
            }
        }
    }
    return CallNextHookEx(wheelHook, code, wParam, lParam);
}

} // namespace

void TrayIcon::installTrayWheelInput()
{
    wheelIcon = trayIcon_;
    if (wheelHook == nullptr)
        wheelHook = SetWindowsHookExW(WH_MOUSE_LL, wheelProc, GetModuleHandleW(nullptr), 0);
}

void TrayIcon::removeTrayWheelInput()
{
    if (wheelIcon == trayIcon_)
        wheelIcon = nullptr;
    if (wheelHook != nullptr) {
        UnhookWindowsHookEx(wheelHook);
        wheelHook = nullptr;
    }
    trayHwnd = nullptr;
}

} // namespace Integration
