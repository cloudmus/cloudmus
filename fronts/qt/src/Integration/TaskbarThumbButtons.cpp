#include "TaskbarThumbButtons.h"

#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QWidget>

#include <windows.h>

#include <shobjidl.h>

#include <cstring>

#include "Icons.h"
#include "MainWindow.h"
#include "PlaybackController.h"
#include "WindowHost.h"

namespace Integration {

namespace {

// Button ids arriving in WM_COMMAND's LOWORD(wParam) once THBN_CLICKED —
// arbitrary but distinctive, to stay well clear of anything else that
// might send WM_COMMAND to this window.
constexpr UINT kButtonPrevious = 40101;
constexpr UINT kButtonPlayPause = 40102;
constexpr UINT kButtonNext = 40103;

// Own literal copies, not shobjidl.h's DEFINE_GUID'd CLSID_TaskbarList /
// IID_ITaskbarList3: those need actual storage from a lib we don't
// otherwise link, same reasoning as SmtcAbiWin.h's own GUID constants.
constexpr GUID kClsidTaskbarList
    = { 0x56fdf344, 0xfd6d, 0x11d0, { 0x95, 0x8a, 0x00, 0x60, 0x97, 0xc9, 0xa0, 0x90 } };
constexpr GUID kIidTaskbarList3
    = { 0xea1afb91, 0x9e28, 0x4b86, { 0x90, 0xe9, 0x9e, 0x9f, 0x8a, 0x5e, 0xef, 0xaf } };

// The taskbar's own light/dark, independent of CloudMus's own theme
// setting (Theme::IconColor) — an icon drawn for the app's current theme
// would often be invisible against the taskbar's. Read once at startup,
// same trade-off as main.cpp's ClearType read: a live system theme flip
// needs a restart to pick up here.
bool systemUsesLightTaskbar()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme",
        RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status != ERROR_SUCCESS || value != 0;
}

// QPixmap -> HICON: a 32bpp top-down DIB section carrying its own alpha
// channel, wrapped with CreateIconIndirect() (the mask bitmap is
// required but unused — CreateIconIndirect honours the DIB's alpha when
// it has one). The caller owns the result (DestroyIcon()).
HICON toHIcon(const QPixmap& pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0)
        return nullptr;

    BITMAPV5HEADER bi {};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = w;
    bi.bV5Height = -h; // negative: top-down, matching QImage's own row order
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00ff0000;
    bi.bV5GreenMask = 0x0000ff00;
    bi.bV5BlueMask = 0x000000ff;
    bi.bV5AlphaMask = 0xff000000;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screenDc);
    if (!color || !bits)
        return nullptr;
    for (int y = 0; y < h; ++y) {
        std::memcpy(static_cast<unsigned char*>(bits) + static_cast<size_t>(y) * w * 4, image.constScanLine(y),
            static_cast<size_t>(w) * 4);
    }

    HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO ii {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

HICON loadButtonIcon(const QString& glyphName, const QColor& color, int pixelSize)
{
    const QIcon themed = Theme::iconFromFile(QStringLiteral(":/icons/symbols/%1.svg").arg(glyphName), color, pixelSize);
    return toHIcon(themed.pixmap(pixelSize, pixelSize));
}

void setButton(THUMBBUTTON& button, UINT id, HICON icon, const wchar_t* tooltip)
{
    button.dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
    button.iId = id;
    button.hIcon = icon;
    lstrcpynW(button.szTip, tooltip, ARRAYSIZE(button.szTip));
    button.dwFlags = THBF_ENABLED;
}

} // namespace

TaskbarThumbButtons::TaskbarThumbButtons(
    Ui::WindowHost& windowHost, Playback::PlaybackController& playback, QObject* parent)
    : QObject(parent)
    , playback_(playback)
{
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarButtonCreated");

    ITaskbarList3* taskbarList = nullptr;
    const HRESULT hr
        = CoCreateInstance(kClsidTaskbarList, nullptr, CLSCTX_INPROC_SERVER, kIidTaskbarList3, reinterpret_cast<void**>(&taskbarList));
    if (FAILED(hr) || !taskbarList) {
        qWarning() << "TaskbarThumbButtons: CoCreateInstance(ITaskbarList3) failed" << Qt::hex << hr;
        return;
    }
    taskbarList->HrInit();
    taskbarList_ = taskbarList;

    const int iconSize = GetSystemMetrics(SM_CXSMICON);
    const QColor iconColor = systemUsesLightTaskbar() ? QColor(Qt::black) : QColor(Qt::white);
    iconPrevious_ = loadButtonIcon(QStringLiteral("skip_previous"), iconColor, iconSize);
    iconPlay_ = loadButtonIcon(QStringLiteral("play_arrow"), iconColor, iconSize);
    iconPause_ = loadButtonIcon(QStringLiteral("pause"), iconColor, iconSize);
    iconNext_ = loadButtonIcon(QStringLiteral("skip_next"), iconColor, iconSize);

    QCoreApplication::instance()->installNativeEventFilter(this);

    connect(&windowHost, &Ui::WindowHost::windowCreated, this, &TaskbarThumbButtons::attachToWindow);
    attachToWindow(windowHost.window()); // the window WindowHost's own constructor already made

    connect(&playback_, &Playback::PlaybackController::playingChanged, this, [this](bool) { updateButtons(); });
    connect(&playback_, &Playback::PlaybackController::currentTrackAvailabilityChanged, this,
        [this](bool) { updateButtons(); });
}

TaskbarThumbButtons::~TaskbarThumbButtons()
{
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
    if (taskbarList_)
        static_cast<ITaskbarList3*>(taskbarList_)->Release();
    for (void* icon : { iconPrevious_, iconPlay_, iconPause_, iconNext_ }) {
        if (icon)
            DestroyIcon(static_cast<HICON>(icon));
    }
}

void TaskbarThumbButtons::attachToWindow(Ui::MainWindow* window)
{
    if (!window || !taskbarList_)
        return;
    // No winId() here: hwnd_ is learned from the TaskbarButtonCreated
    // message in nativeEventFilter(), which Explorer only sends once the
    // window is shown and has a real taskbar button.
    currentWindow_ = window;
    hwnd_ = 0;
    buttonsRegistered_ = false;
}

void TaskbarThumbButtons::registerButtons()
{
    if (!taskbarList_ || !hwnd_)
        return;
    THUMBBUTTON buttons[3] {};
    setButton(buttons[0], kButtonPrevious, static_cast<HICON>(iconPrevious_), L"Previous");
    setButton(buttons[1], kButtonPlayPause, static_cast<HICON>(playback_.isPlaying() ? iconPause_ : iconPlay_),
        playback_.isPlaying() ? L"Pause" : L"Play");
    setButton(buttons[2], kButtonNext, static_cast<HICON>(iconNext_), L"Next");

    const HRESULT hr
        = static_cast<ITaskbarList3*>(taskbarList_)->ThumbBarAddButtons(reinterpret_cast<HWND>(hwnd_), 3, buttons);
    if (FAILED(hr)) {
        qWarning() << "TaskbarThumbButtons: ThumbBarAddButtons failed" << Qt::hex << hr;
        return;
    }
    buttonsRegistered_ = true;
}

void TaskbarThumbButtons::updateButtons()
{
    if (!buttonsRegistered_ || !taskbarList_ || !hwnd_)
        return;
    THUMBBUTTON button {};
    setButton(button, kButtonPlayPause, static_cast<HICON>(playback_.isPlaying() ? iconPause_ : iconPlay_),
        playback_.isPlaying() ? L"Pause" : L"Play");
    static_cast<ITaskbarList3*>(taskbarList_)->ThumbBarUpdateButtons(reinterpret_cast<HWND>(hwnd_), 1, &button);
}

bool TaskbarThumbButtons::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const auto* msg = static_cast<const MSG*>(message);

    // TaskbarButtonCreated is a system-wide broadcast (every top-level
    // window in this process sees it, SmtcService's own hidden one
    // included) — QWidget::find() (same technique as PopupWindow.cpp's
    // ClickThroughFilter) confirms it's actually our current MainWindow
    // before trusting its hwnd.
    if (taskbarCreatedMessage_ != 0 && msg->message == taskbarCreatedMessage_) {
        if (QWidget::find(reinterpret_cast<WId>(msg->hwnd)) == currentWindow_) {
            hwnd_ = reinterpret_cast<quintptr>(msg->hwnd);
            registerButtons();
        }
        return false; // Qt/DefWindowProc still see it — nothing to suppress
    }

    if (reinterpret_cast<quintptr>(msg->hwnd) != hwnd_)
        return false;
    if (msg->message == WM_COMMAND && HIWORD(msg->wParam) == THBN_CLICKED) {
        switch (LOWORD(msg->wParam)) {
            case kButtonPrevious:
                playback_.previous();
                break;
            case kButtonPlayPause:
                playback_.togglePause();
                break;
            case kButtonNext:
                playback_.next();
                break;
            default:
                break;
        }
        if (result)
            *result = 0;
        return true;
    }
    return false;
}

} // namespace Integration
