#include "NotificationToast.h"

#include <QCoreApplication>
#include <QImage>
#include <QSystemTrayIcon>
#include <QTimer>

#include <iterator>

#include <windows.h>

#include <shellapi.h>

namespace Integration {

namespace {

// How long a track's balloon waits for a cover that's still loading.
constexpr int kCoverWaitMs = 700;

// Qt's tray icon, as Shell_NotifyIcon() knows it: Qt registers it with
// uID 0 on a hidden window of its own, titled "QTrayIconMessageWindow"
// (qwindowssystemtrayicon.cpp) — no public API hands either out.
HWND qtTrayWindow()
{
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
    return search.found;
}

void copyTruncated(const QString& text, wchar_t* target, int capacity)
{
    const QString truncated = text.left(capacity - 1);
    truncated.toWCharArray(target);
    target[truncated.size()] = L'\0';
}

} // namespace

NotificationToast::NotificationToast(QSystemTrayIcon* trayIcon, QObject* parent)
    : QObject(parent)
    , trayIcon_(trayIcon)
    , coverWait_(new QTimer(this))
{
    // A balloon we send ourselves is still Qt's icon's: its click reaches
    // Qt's window procedure, which emits messageClicked() as for its own.
    if (trayIcon_)
        connect(trayIcon_, &QSystemTrayIcon::messageClicked, this, [this]() { emit activated(QString()); });
    coverWait_->setSingleShot(true);
    coverWait_->setInterval(kCoverWaitMs);
    connect(coverWait_, &QTimer::timeout, this, [this]() { showBalloon(waitingTitle_, waitingArtist_, QPixmap()); });
}

void NotificationToast::showTrackChange(const QString& title, const QString& artist, const QPixmap& cover)
{
    if (!cover.isNull()) {
        coverWait_->stop();
        showBalloon(title, artist, cover);
        return;
    }
    // Its cover may be a moment away (main.cpp asks updateCover() then).
    waitingTitle_ = title;
    waitingArtist_ = artist;
    coverWait_->start();
}

void NotificationToast::updateCover(const QString& title, const QString& artist, const QPixmap& cover)
{
    if (!coverWait_->isActive() || title != waitingTitle_ || artist != waitingArtist_)
        return; // its balloon has gone out already, without the cover
    coverWait_->stop();
    showBalloon(title, artist, cover);
}

void NotificationToast::showBalloon(const QString& title, const QString& artist, const QPixmap& cover)
{
    if (!trayIcon_ || !trayIcon_->isVisible())
        return;
    // QSystemTrayIcon::showMessage() can't be made silent: Qt never sets
    // NIIF_NOSOUND, so Windows chimed on every track. The same balloon,
    // sent directly, can be.
    const HWND hwnd = qtTrayWindow();
    if (hwnd == nullptr) {
        trayIcon_->showMessage(title, artist, QSystemTrayIcon::Information, 5000);
        return;
    }
    NOTIFYICONDATAW data = { };
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = 0;
    data.uFlags = NIF_INFO | NIF_SHOWTIP;
    data.uTimeout = 5000;
    // As Qt does: an empty text shows no balloon at all.
    copyTruncated(artist.isEmpty() ? QStringLiteral(" ") : artist, data.szInfo, int(std::size(data.szInfo)));
    copyTruncated(title, data.szInfoTitle, int(std::size(data.szInfoTitle)));
    data.dwInfoFlags = NIIF_NOSOUND;
    HICON icon = cover.isNull() ? nullptr : cover.toImage().toHICON();
    if (icon != nullptr) {
        data.dwInfoFlags |= NIIF_USER | NIIF_LARGE_ICON;
        data.hBalloonIcon = icon;
    }
    Shell_NotifyIconW(NIM_MODIFY, &data);
    if (balloonIcon_ != nullptr)
        DestroyIcon(static_cast<HICON>(balloonIcon_));
    balloonIcon_ = icon;
}

void NotificationToast::onNotificationClosed(uint, uint) { }
void NotificationToast::onActionInvoked(uint, const QString&) { }
void NotificationToast::onActivationToken(uint, const QString&) { }

} // namespace Integration

#include "moc_NotificationToast.cpp"
