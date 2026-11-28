#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>

#include "Core.h"
#include "DownloadsScreen.h"
#include "ImitatedMusic.h"
#include "LibraryScreen.h"
#include "MainScreen.h"
#include "MediaTree.h"
#include "PlatformSettings.h"
#include "SignInHelperDownload.h"
#include "PlaylistScreens.h"
#include "PulseScreen.h"
#include "SettingsScreen.h"
#include "SourceSettings.h"
#include "SourceSettingsScreen.h"
#include "TrackScreen.h"
#include "UpdateChecker.h"
#include "UpdateScreen.h"

namespace App {

// What the mobile front runs: the desktop's App::Core, plus the settings only
// a phone or a car has. Lives for the whole run of the process it's in: the
// playback service on Android, the app itself on the desktop.
class MobileCore : public QObject {
    Q_OBJECT

public:
    struct Paths {
        QString settingsFile;
        // See Rpc::SourceManager::setSpawnDelay() and
        // ViewModel::ActivePlaylist::setRefreshDelay(): for the phone and the
        // car, where a start has to be quick on little CPU.
        int spawnDelayMs = 0;
        int refreshDelayMs = 0;
        // The platform's media library (Rpc::SourceManager::setHostMedia),
        // if it has one.
        Rpc::HostMedia* hostMedia = nullptr;
        // Where an update of the app is downloaded to; none: this build
        // can't install one by itself (the release page instead).
        QString updatesDir;
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
    // The first level's, the one the Library opens.
    ViewModel::PlaylistScreen& playlistScreen() { return playlistScreens_.at(0); }
    ViewModel::PlaylistScreens& playlistScreens() { return playlistScreens_; }
    ViewModel::TrackScreen& trackScreen() { return trackScreen_; }
    ViewModel::MediaTree& mediaTree() { return mediaTree_; }
    ViewModel::SettingsScreen& settingsScreen() { return settingsScreen_; }
    ViewModel::SourceSettingsScreen& sourceSettingsScreen() { return sourceSettingsScreen_; }
    ViewModel::PulseScreen& pulseScreen() { return pulseScreen_; }
    ViewModel::UpdateScreen& updateScreen() { return updateScreen_; }
    ViewModel::SignInHelperDownload& signInHelper() { return signInHelper_; }

    // The sign-in on screen (LibraryScreen::watchSignIn) waiting for its code
    // to be confirmed elsewhere, in a browser, for the platform to keep the
    // process alive meanwhile (Android's SignInService): an empty title when
    // there is none.
    struct SignInNotice {
        QString title;
        QString text;
        QString channelName;
        bool operator==(const SignInNotice&) const = default;
    };
    SignInNotice signInNotice() const;

signals:
    void signInNoticeChanged();

private:
    // Developer tools' audio output and soft edges (PlatformSettings), kept
    // applied to the player.
    void applyPlaybackExperiments();
    void leaveUnstartedWhatAskedForSignIn();
    void updateSignInNotice();
    void setUpUpdates(const QString& updatesDir);

    QSettings settingsStore_;
    // Sources that were signing in when the app was unloaded, to be shown
    // again when they come back up (leaveUnstartedWhatAskedForSignIn()).
    QStringList resumingSignIn_;
    SignInNotice signInNotice_;
    Core core_;
    ViewModel::PlatformSettings settings_;
    ViewModel::LibraryScreen library_;
    ViewModel::MainScreen mainScreen_;
    ViewModel::DownloadsScreen downloadsScreen_;
    ViewModel::PlaylistScreens playlistScreens_;
    ViewModel::TrackScreen trackScreen_;
    ViewModel::MediaTree mediaTree_;
    ViewModel::SourceSettings sourceSettings_;
    ViewModel::SettingsScreen settingsScreen_;
    ViewModel::SourceSettingsScreen sourceSettingsScreen_;
#ifdef Q_OS_ANDROID
    // Android's libmpv can't analyze the music (docs/android.md, "a libmpv of
    // our own"): the views move with music made up meanwhile.
    ViewModel::ImitatedMusic imitatedMusic_;
    ViewModel::AudioPulse imitatedPulse_;
#endif
    ViewModel::PulseScreen pulseScreen_;
    Update::UpdateChecker updates_;
    ViewModel::UpdateScreen updateScreen_;
    ViewModel::SignInHelperDownload signInHelper_;
};

} // namespace App
