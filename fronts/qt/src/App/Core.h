#pragma once

#include <QObject>

#include "ActivePlaylist.h"
#include "Analytics.h"
#include "AuthStates.h"
#include "Browse.h"
#include "CoverArtCache.h"
#include "Downloads.h"
#include "Messages.h"
#include "NowPlaying.h"
#include "PlaybackController.h"
#include "PlaybackHistory.h"
#include "PlaylistEditing.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourcePage.h"
#include "SourceSession.h"
#include "Sources.h"
#include "TrackStates.h"
#include "UpdateChecker.h"

namespace App {

// Everything the app is, short of its window: settings, the backends,
// playback and the state kept about them. Created once in main() and alive
// for the whole run — the window (and the tray, MPRIS/SMTC, notifications)
// only use it, so a window can come and go without anything here being
// lost.
class Core : public QObject {
    Q_OBJECT

public:
    explicit Core(QObject* parent = nullptr);

    Config::Settings& settings() { return settings_; }
    Analytics& analytics() { return analytics_; }
    Rpc::SourceManager& sourceManager() { return sourceManager_; }
    Playback::PlaybackController& playback() { return playback_; }
    Rpc::AuthStates& authStates() { return authStates_; }
    History::PlaybackHistory& playbackHistory() { return playbackHistory_; }
    Library::TrackStates& trackStates() { return trackStates_; }
    Covers::CoverArtCache& coverArtCache() { return coverArtCache_; }
    SourceSession& sourceSession() { return sourceSession_; }
    ViewModel::Messages& messages() { return messages_; }
    ViewModel::NowPlaying& nowPlaying() { return nowPlaying_; }
    ViewModel::Downloads& downloads() { return downloads_; }
    PlaylistEditing& playlistEditing() { return playlistEditing_; }
    ViewModel::Sources& sources() { return sources_; }
    ViewModel::ActivePlaylist& activePlaylist() { return activePlaylist_; }
    ViewModel::Browse& browse() { return browse_; }
    ViewModel::SourcePage& sourcePage() { return sourcePage_; }
    Update::UpdateChecker& updates() { return updates_; }

private:
    // Declaration order is construction order: playback_ needs
    // sourceManager_.
    Config::Settings settings_;
    Analytics analytics_;
    Rpc::SourceManager sourceManager_;
    Playback::PlaybackController playback_;
    Rpc::AuthStates authStates_;
    History::PlaybackHistory playbackHistory_;
    Library::TrackStates trackStates_;
    Covers::CoverArtCache coverArtCache_;
    ViewModel::Messages messages_;
    SourceSession sourceSession_;
    ViewModel::Downloads downloads_;
    ViewModel::NowPlaying nowPlaying_;
    PlaylistEditing playlistEditing_;
    ViewModel::Sources sources_;
    ViewModel::ActivePlaylist activePlaylist_;
    // After activePlaylist_: it restores its page once the active
    // playlist has restored itself.
    ViewModel::Browse browse_;
    ViewModel::SourcePage sourcePage_;
    Update::UpdateChecker updates_;
};

} // namespace App
