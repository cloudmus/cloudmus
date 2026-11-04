#pragma once

#include <QObject>
#include <QSettings>

#include "Core.h"
#include "DownloadsScreen.h"
#include "LibraryScreen.h"
#include "MainScreen.h"
#include "PlatformSettings.h"
#include "PlaylistScreen.h"
#include "TrackScreen.h"

namespace App {

// What the mobile front runs: the desktop's App::Core, plus the settings only
// a phone or a car has. Lives for the whole run of the process it's in: the
// playback service on Android, the app itself on the desktop.
class MobileCore : public QObject {
    Q_OBJECT

public:
    struct Paths {
        QString media; // not used here: see src/DevTracks
        QString settingsFile;
        // See Rpc::SourceManager::setSpawnDelay() and
        // ViewModel::ActivePlaylist::setRefreshDelay(): for the phone and the
        // car, where a start has to be quick on little CPU.
        int spawnDelayMs = 0;
        int refreshDelayMs = 0;
    };

    explicit MobileCore(const Paths& paths, QObject* parent = nullptr);

    Core& core() { return core_; }
    ViewModel::NowPlaying& nowPlaying() { return core_.nowPlaying(); }
    ViewModel::ActivePlaylist& activePlaylist() { return core_.activePlaylist(); }
    ViewModel::TrackFilterModel& tracks() { return mainScreen_.rows(); }
    ViewModel::MainScreen& mainScreen() { return mainScreen_; }
    ViewModel::DownloadsScreen& downloadsScreen() { return downloadsScreen_; }
    ViewModel::PlatformSettings& settings() { return settings_; }
    ViewModel::LibraryScreen& library() { return library_; }
    ViewModel::PlaylistScreen& playlistScreen() { return playlistScreen_; }
    ViewModel::TrackScreen& trackScreen() { return trackScreen_; }

private:
    void leaveUnstartedWhatAskedForSignIn();

    QSettings settingsStore_;
    Core core_;
    ViewModel::PlatformSettings settings_;
    ViewModel::LibraryScreen library_;
    ViewModel::MainScreen mainScreen_;
    ViewModel::DownloadsScreen downloadsScreen_;
    ViewModel::PlaylistScreen playlistScreen_;
    ViewModel::TrackScreen trackScreen_;
};

} // namespace App
