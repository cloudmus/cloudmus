#pragma once

#include <QObject>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "Messages.h"
#include "NowPlaying.h"
#include "PlaybackController.h"
#include "PlaybackHistory.h"
#include "PlaylistEditing.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "TrackStates.h"

namespace App {

// Everything the app is, short of its window: settings, the backends,
// playback and the state kept about them. Created once in main() and alive
// for the whole run — the window (and the tray, MPRIS, notifications) only
// use it, so a window can come and go without anything here being lost.
// See docs/mvvm-plan.md: view models join these as the plan's stages land.
class Core : public QObject {
    Q_OBJECT

public:
    explicit Core(QObject* parent = nullptr);

    Config::Settings& settings() { return settings_; }
    Rpc::SourceManager& sourceManager() { return sourceManager_; }
    Playback::PlaybackController& playback() { return playback_; }
    Rpc::AuthStates& authStates() { return authStates_; }
    History::PlaybackHistory& playbackHistory() { return playbackHistory_; }
    Library::TrackStates& trackStates() { return trackStates_; }
    Covers::CoverArtCache& coverArtCache() { return coverArtCache_; }
    SourceSession& sourceSession() { return sourceSession_; }
    ViewModel::Messages& messages() { return messages_; }
    ViewModel::NowPlaying& nowPlaying() { return nowPlaying_; }
    PlaylistEditing& playlistEditing() { return playlistEditing_; }

private:
    // Declaration order is construction order: playback_ needs
    // sourceManager_.
    Config::Settings settings_;
    Rpc::SourceManager sourceManager_;
    Playback::PlaybackController playback_;
    Rpc::AuthStates authStates_;
    History::PlaybackHistory playbackHistory_;
    Library::TrackStates trackStates_;
    Covers::CoverArtCache coverArtCache_;
    ViewModel::Messages messages_;
    SourceSession sourceSession_;
    ViewModel::NowPlaying nowPlaying_;
    PlaylistEditing playlistEditing_;
};

} // namespace App
