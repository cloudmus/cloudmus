#pragma once

#include <QObject>
#include <QSettings>

#include <memory>

#include "ActivePlaylist.h"
#include "NowPlaying.h"
#include "QueuePlayer.h"
#include "PlatformSettings.h"

namespace App {

// The APK's core until the backends run on Android: three dev tracks instead
// of App::Core (see src/App/MobileCore.h, which replaces this). Lives for the whole run of the process it's in: the
// playback service on Android, the app itself on the desktop.
class MobileCore : public QObject {
    Q_OBJECT

public:
    struct Paths {
        QString media; // holds tracks.json
        QString settingsFile;
    };

    explicit MobileCore(const Paths& paths, QObject* parent = nullptr);

    QSettings& settingsStore() { return settingsStore_; }
    Playback::QueuePlayer& player() { return player_; }
    ViewModel::NowPlaying& nowPlaying() { return nowPlaying_; }
    ViewModel::ActivePlaylist& activePlaylist() { return activePlaylist_; }
    ViewModel::TrackListModel& tracks() { return activePlaylist_.tracks(); }
    ViewModel::PlatformSettings& settings() { return settings_; }

private:
    QSettings settingsStore_;
    Playback::QueuePlayer player_;
    ViewModel::NowPlaying nowPlaying_;
    ViewModel::ActivePlaylist activePlaylist_;
    ViewModel::PlatformSettings settings_;
};

} // namespace App
