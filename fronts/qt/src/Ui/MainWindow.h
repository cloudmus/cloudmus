#pragma once

#include <QMainWindow>

#include "Coro.h"
#include "PlaybackController.h"
#include "Settings.h"
#include "SourceManager.h"

class QTreeView;
class QListView;
class QModelIndex;
class QProgressBar;
class QCloseEvent;

namespace History {
class PlaybackHistory;
}

namespace Ui {

class SidebarModel;
class TrackListModel;
class TrackRowDelegate;
class NowPlayingBar;
class AuthBanner;
class ToastNotifier;
class CoverArtCache;
class PlaylistHeader;

// Sidebar + track list + persistent now-playing bar + auth banner +
// hamburger menu (Settings/About/Quit) — see the plan's UI/UX design.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(Rpc::SourceManager& sourceManager, Playback::PlaybackController& playback, Config::Settings& settings,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    // Called by the tray's Quit action — bypasses close-to-tray.
    void quitForReal();

signals:
    void aboutToReallyQuit();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void wireSource(Rpc::RpcClient* client);
    void onSourceUnavailable(const QString& manifestId, const QString& name, QStringList stderrTail);
    void onSidebarActivated(const QModelIndex& index);
    void onSidebarDoubleClicked(const QModelIndex& index);
    void onTrackDoubleClicked(const QModelIndex& index);
    // Shared by the header's Play button and onSidebarDoubleClicked() — both
    // just need "start playing whatever's currently shown" once it's
    // loaded (currentPlaylistSourceId_/currentPlaylist_/trackListModel_).
    void playCurrentPlaylist();
    void showAboutDialog();
    // Synchronous, unlike showPlaylistAsync() — History is local state, no
    // RPC round-trip needed. See History::PlaybackHistory.
    void showHistory();

    Rpc::Task<void> loadPlaylistsAsync(Rpc::RpcClient* client);
    // By value, not const&: these coroutines resume asynchronously (after an
    // RPC round-trip) and use their params again after that resume — a
    // reference to a caller's temporary/local (as with detach()ed calls
    // from onSidebarActivated) would dangle by the time execution gets back
    // there. See the equivalent comment on RpcClient::call() for the full
    // explanation of this coroutine-lifetime pitfall.
    //
    // A single click on a sidebar item only shows its PlaylistHeader
    // (cover/title/description) and, for a browsable kind, its track list —
    // it never starts playback by itself (see docs/protocol.md's
    // Playlist.kind note and the plan for why My Wave must not auto-play on
    // select). Playback starts from PlaylistHeader's Play button, a track
    // double-click, or double-clicking the sidebar item itself
    // (onSidebarDoubleClicked, which awaits this and then calls
    // playCurrentPlaylist()).
    Rpc::Task<void> showPlaylistAsync(QString sourceId, Playlist playlist);
    // onSidebarDoubleClicked()'s handler: awaits showPlaylistAsync() (so the
    // double-clicked item is loaded regardless of what was shown before),
    // then plays it via playCurrentPlaylist().
    Rpc::Task<void> openAndPlayPlaylistAsync(QString sourceId, Playlist playlist);
    Rpc::Task<void> startRadioAsync(QString sourceId, QString seed);
    Rpc::Task<void> submitAuthAsync(QString sourceId, QJsonObject fields);

    Rpc::SourceManager& sourceManager_;
    Playback::PlaybackController& playback_;
    Config::Settings& settings_;

    void repositionTrackListBusyIndicator();

    SidebarModel* sidebarModel_ = nullptr;
    QTreeView* sidebarView_ = nullptr;
    PlaylistHeader* playlistHeader_ = nullptr;
    TrackListModel* trackListModel_ = nullptr;
    QListView* trackListView_ = nullptr;
    QProgressBar* trackListBusyIndicator_ = nullptr;
    CoverArtCache* coverArtCache_ = nullptr;
    TrackRowDelegate* trackRowDelegate_ = nullptr;
    NowPlayingBar* nowPlayingBar_ = nullptr;
    AuthBanner* authBanner_ = nullptr;
    ToastNotifier* toastNotifier_ = nullptr;
    History::PlaybackHistory* playbackHistory_ = nullptr;

    // Stashed so PlaylistHeader's Play button (clicked well after
    // showPlaylistAsync returns) knows what to start — see its handler in
    // the .cpp.
    QString currentPlaylistSourceId_;
    Playlist currentPlaylist_;
    // True while the sidebar's History entry is the active selection, so a
    // live PlaybackHistory::changed() (a track just started playing) knows
    // to refresh the view instead of touching it while some other playlist
    // is showing.
    bool showingHistory_ = false;

    bool reallyQuitting_ = false;
};

} // namespace Ui
