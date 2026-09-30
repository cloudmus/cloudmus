#pragma once

#include <QObject>

#include <memory>

namespace Playback {
class PlaybackController;
}
namespace Covers {
class CoverArtCache;
}

namespace Integration {

// Windows' System Media Transport Controls (SMTC) — the Windows counterpart
// of MprisService: hardware/keyboard media keys, the volume flyout's media
// card (Windows 10) or the quick-settings media card (Windows 11), and the
// lock screen, all fed from the same PlaybackController. No Raise/Quit
// here: SMTC has no such affordance (contrast org.mpris.MediaPlayer2, which
// doubles as a window-activation channel).
//
// A pimpl on purpose: this file is included from main.cpp, and keeping the
// COM/WinRT machinery (SmtcAbiWin.h, <windows.h>, a hidden HWND, event
// tokens, ...) out of the header keeps that one TU's include graph plain
// Qt. See SmtcService.cpp for the implementation.
class SmtcService : public QObject {
    Q_OBJECT

public:
    // `playback`/`covers` must outlive this service, same contract as
    // MprisService's constructor.
    explicit SmtcService(
        Playback::PlaybackController& playback, Covers::CoverArtCache& covers, QObject* parent = nullptr);
    ~SmtcService() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Integration
