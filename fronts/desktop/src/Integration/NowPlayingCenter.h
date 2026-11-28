#pragma once

#include <QObject>

#include <memory>

namespace Covers {
class CoverArtCache;
}
namespace Playback {
class PlaybackController;
}
namespace ViewModel {
class NowPlaying;
}

namespace Integration {

// macOS's Now Playing (MediaPlayer.framework) — the macOS counterpart of
// MprisService and SmtcService: the track, its cover and progress in
// Control Center and the menu bar's Now Playing, on the lock screen and
// the Touch Bar; the media keys (F7–F9), AirPods and headset buttons and
// those places' own controls play, seek, shuffle, repeat, like and dislike.
//
// A pimpl: the Objective-C side stays in NowPlayingCenterMac.mm, so
// main.cpp includes plain C++.
class NowPlayingCenter : public QObject {
    Q_OBJECT

public:
    // `playback`/`nowPlaying`/`covers` must outlive this, as for
    // MprisService.
    NowPlayingCenter(Playback::PlaybackController& playback, ViewModel::NowPlaying& nowPlaying,
        Covers::CoverArtCache& covers, QObject* parent = nullptr);
    ~NowPlayingCenter() override;

signals:
    // A media key or the system's media controls sent a playback command.
    void commandReceived();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Integration
