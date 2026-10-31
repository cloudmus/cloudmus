#pragma once

#include <QObject>
#include <QSettings>

#include <memory>

#include "ActivePlaylist.h"
#include "NowPlaying.h"
#include "QueuePlayer.h"
#include "Settings.h"

namespace App {

// Everything the mobile front is short of its views — the MVP's counterpart
// of the desktop App::Core, laid out the same way so pieces of that one can
// move in later. Lives for the whole run of the process it's in: the
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
    ViewModel::Settings& settings() { return settings_; }

private:
    QSettings settingsStore_;
    Playback::QueuePlayer player_;
    ViewModel::NowPlaying nowPlaying_;
    ViewModel::ActivePlaylist activePlaylist_;
    ViewModel::Settings settings_;
};

} // namespace App
