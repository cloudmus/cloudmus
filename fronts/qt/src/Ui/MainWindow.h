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

namespace Ui {

class SidebarModel;
class TrackListModel;
class TrackRowDelegate;
class NowPlayingBar;
class AuthBanner;
class ToastNotifier;
class CoverArtCache;

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
    void onTrackDoubleClicked(const QModelIndex& index);
    void showAboutDialog();

    Rpc::Task<void> loadPlaylistsAsync(Rpc::RpcClient* client);
    // By value, not const&: these coroutines resume asynchronously (after an
    // RPC round-trip) and use sourceId/playlistId again after that resume —
    // a reference to a caller's temporary/local (as with detach()ed calls
    // from onSidebarActivated) would dangle by the time execution gets back
    // there. See the equivalent comment on RpcClient::call() for the full
    // explanation of this coroutine-lifetime pitfall.
    Rpc::Task<void> loadTracksAsync(QString sourceId, QString playlistId);
    Rpc::Task<void> loadLikedAsync(QString sourceId);
    Rpc::Task<void> startRadioAsync(QString sourceId);
    Rpc::Task<void> submitAuthAsync(QString sourceId, QJsonObject fields);

    Rpc::SourceManager& sourceManager_;
    Playback::PlaybackController& playback_;
    Config::Settings& settings_;

    SidebarModel* sidebarModel_ = nullptr;
    QTreeView* sidebarView_ = nullptr;
    TrackListModel* trackListModel_ = nullptr;
    QListView* trackListView_ = nullptr;
    QProgressBar* trackListBusyIndicator_ = nullptr;
    CoverArtCache* coverArtCache_ = nullptr;
    TrackRowDelegate* trackRowDelegate_ = nullptr;
    NowPlayingBar* nowPlayingBar_ = nullptr;
    AuthBanner* authBanner_ = nullptr;
    ToastNotifier* toastNotifier_ = nullptr;

    bool reallyQuitting_ = false;
};

} // namespace Ui
