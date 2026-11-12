#include "DesktopCore.h"

namespace App {

DesktopCore::DesktopCore(QObject* parent)
    : Core(parent)
    , browse_(sourceManager(), trackStates(), coverArtCache(), playbackHistory(), settings(), sources(),
          activePlaylist(), playlistEditing(), messages())
    , hotkeys_(settings())
    , hotkeyDispatcher_(nowPlaying(), hotkeys_)
    , updates_(settings())
    , starPrompt_(settings(), analytics())
{
    connect(&hotkeyDispatcher_, &Hotkeys::Dispatcher::cueRequested, &cuePlayer_, &Playback::CuePlayer::play);
    connect(&playback(), &Playback::PlaybackController::trackChanged, &starPrompt_,
        [this](const Track&, const QString&) { starPrompt_.recordUsage(); });
}

} // namespace App
