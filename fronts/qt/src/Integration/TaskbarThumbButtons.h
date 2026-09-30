#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>

namespace Playback {
class PlaybackController;
}
namespace Ui {
class WindowHost;
class MainWindow;
}

namespace Integration {

// Previous/Play-Pause/Next buttons on the live thumbnail Windows shows
// when hovering CloudMus's taskbar icon (ITaskbarList3's "thumbnail
// toolbar") — Windows-only, and not something SmtcService's SMTC
// registration gives for free: SMTC covers hardware keys, the lock screen
// and the volume flyout's/quick settings' media card, but not the
// taskbar thumbnail's own toolbar.
//
// Follows Ui::WindowHost across a window recreation (Settings' glass
// toggle): a new window means a new native HWND, so the taskbar button —
// and the thumbbar buttons registered on it — need to be redone from
// scratch. HWND and HICON are kept as plain integers/pointers here so this
// header stays free of <windows.h>; see TaskbarThumbButtons.cpp.
class TaskbarThumbButtons : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    // `playback` must outlive this, same contract as SmtcService's.
    explicit TaskbarThumbButtons(
        Ui::WindowHost& windowHost, Playback::PlaybackController& playback, QObject* parent = nullptr);
    ~TaskbarThumbButtons() override;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void attachToWindow(Ui::MainWindow* window);
    void registerButtons();
    void updateButtons();

    Playback::PlaybackController& playback_;
    void* taskbarList_ = nullptr; // ITaskbarList3*
    Ui::MainWindow* currentWindow_ = nullptr; // non-owning, to tell "our" TaskbarButtonCreated from anyone else's
    quintptr hwnd_ = 0;
    unsigned int taskbarCreatedMessage_ = 0;
    bool buttonsRegistered_ = false;
    void* iconPrevious_ = nullptr; // HICON
    void* iconPlay_ = nullptr; // HICON
    void* iconPause_ = nullptr; // HICON
    void* iconNext_ = nullptr; // HICON
};

} // namespace Integration
