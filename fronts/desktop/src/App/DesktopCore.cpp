#include "DesktopCore.h"

namespace App {

DesktopCore::DesktopCore(QObject* parent)
    : Core(parent)
    , browse_(sourceManager(), trackStates(), coverArtCache(), playbackHistory(), settings(), sources(),
          activePlaylist(), playlistEditing(), messages(), playlistTracks())
    , hotkeys_(settings())
    , hotkeyDispatcher_(nowPlaying(), hotkeys_)
    , updates_(settings())
    , starPrompt_(settings(), analytics())
{
    // The offline indicator beside the menu button says it, not a toast for
    // each thing that fails for lack of a network.
    messages().setOfflineShownElsewhere(true);
    connect(&trackDeletion(), &TrackDeletion::deleted, &browse_, &ViewModel::Browse::dropTracks);
    connect(&hotkeyDispatcher_, &Hotkeys::Dispatcher::cueRequested, &cuePlayer_, &Playback::CuePlayer::play);
    connect(&playback(), &Playback::PlaybackController::trackChanged, &starPrompt_,
        [this](const Track&, const QString&) { starPrompt_.recordUsage(); });
}

} // namespace App
