#pragma once

#include <QObject>
#include <QSettings>

#include "Core.h"
#include "LibraryScreen.h"
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
    };

    explicit MobileCore(const Paths& paths, QObject* parent = nullptr);

    Core& core() { return core_; }
    ViewModel::NowPlaying& nowPlaying() { return core_.nowPlaying(); }
    ViewModel::ActivePlaylist& activePlaylist() { return core_.activePlaylist(); }
    ViewModel::ActiveTracks& tracks() { return core_.activeTracks(); }
    ViewModel::PlatformSettings& settings() { return settings_; }
    ViewModel::LibraryScreen& library() { return library_; }
    ViewModel::PlaylistScreen& playlistScreen() { return playlistScreen_; }
    ViewModel::TrackScreen& trackScreen() { return trackScreen_; }

private:
    QSettings settingsStore_;
    Core core_;
    ViewModel::PlatformSettings settings_;
    ViewModel::LibraryScreen library_;
    ViewModel::PlaylistScreen playlistScreen_;
    ViewModel::TrackScreen trackScreen_;
};

} // namespace App
