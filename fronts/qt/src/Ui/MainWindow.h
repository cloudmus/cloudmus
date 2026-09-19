#pragma once

#include <QHash>
#include <QJsonObject>
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
class QSplitter;

namespace History {
class PlaybackHistory;
}

namespace Ui {

class SidebarModel;
class NavItemDelegate;
class TrackListModel;
class TrackRowDelegate;
class NowPlayingBar;
class SourcePanel;
class ToastNotifier;
class CoverArtCache;
class HeroPanel;
class EmptyStatePlaceholder;

// Sidebar + track list + persistent now-playing bar + per-source auth status
// panel + hamburger menu (Settings/About/Quit) — see the plan's UI/UX design.
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

    // Mirrors the TUI's on_mount auth check (fronts/tui/cloudmus_tui/app.py):
    // ask auth.getStatus, and if the backend isn't already authenticated,
    // call auth.start so it begins its flow and starts pushing auth/prompt /
    // auth/statusChanged notifications. Without this, a never-authenticated
    // backend just sits idle — SourcePanel has full rendering support for
    // all three flows but nothing ever asks the backend to start one.
    Rpc::Task<void> ensureAuthenticatedAsync(Rpc::RpcClient* client);
    Rpc::Task<void> loadPlaylistsAsync(Rpc::RpcClient* client);
    // By value, not const&: these coroutines resume asynchronously (after an
    // RPC round-trip) and use their params again after that resume — a
    // reference to a caller's temporary/local (as with detach()ed calls
    // from onSidebarActivated) would dangle by the time execution gets back
    // there. See the equivalent comment on RpcClient::call() for the full
    // explanation of this coroutine-lifetime pitfall.
    //
    // A single click on a sidebar item only shows its HeroPanel promo card
    // (cover/title/description, unless something's already playing — see
    // Playback::PlaybackController::hasCurrentTrack()) and, for a
    // browsable kind, its track list — it never starts playback by itself (see
    // docs/protocol.md's Playlist.kind note and the plan for why My Wave
    // must not auto-play on select). Playback starts from HeroPanel's Play
    // button, a track double-click, or double-clicking the sidebar item itself
    // (onSidebarDoubleClicked, which awaits this and then calls
    // playCurrentPlaylist()).
    Rpc::Task<void> showPlaylistAsync(QString sourceId, Playlist playlist);
    // onSidebarDoubleClicked()'s handler: awaits showPlaylistAsync() (so the
    // double-clicked item is loaded regardless of what was shown before),
    // then plays it via playCurrentPlaylist().
    Rpc::Task<void> openAndPlayPlaylistAsync(QString sourceId, Playlist playlist);
    Rpc::Task<void> startRadioAsync(QString sourceId, QString seed);
    Rpc::Task<void> submitAuthAsync(QString sourceId, QJsonObject fields);
    // The Retry button's handler: wraps ensureAuthenticatedAsync with
    // sourcePanel_'s busy state (disables Retry/Submit + shows a spinner
    // for the duration) so a click can't be repeated mid-flight and the
    // user sees something actually happened.
    Rpc::Task<void> retryAuthAsync(QString sourceId);

    // Per-source auth status, cached here since nothing on RpcClient itself
    // persists it (onAuthPromptRaw/notifications.onAuthStatusChanged are
    // fire-and-forget pushes — see wireSource()). Feeds both the sidebar's
    // warning icon (SidebarModel::setSourceAuthProblem, always applied,
    // regardless of whether the panel is currently open for that source)
    // and sourcePanel_'s auth section (only when it's the currently-selected
    // source).
    struct SourceAuthState {
        bool hasProblem = false; // capabilities.auth.required && not authenticated
        QJsonObject prompt; // last auth/prompt payload; empty if none yet
        QString errorMessage; // last auth/statusChanged error message; empty if none
    };
    QHash<QString, SourceAuthState> sourceAuthStates_;
    // sourceId sourcePanel_ is currently showing, or empty if it's hidden /
    // a normal playlist is showing instead.
    QString currentStatusPanelSourceId_;

    // Updates the sidebar icon for sourceId from sourceAuthStates_, and — if
    // sourcePanel_ is currently showing exactly this source — its auth
    // section too, so a prompt/status update arriving while the panel is
    // already open refreshes it live instead of needing a re-click.
    void updateSourceAuthIndicator(const QString& sourceId);
    // Entry point from onSidebarActivated: swaps the content area over to
    // sourcePanel_ for this source (every source gets this, not just ones
    // with an auth problem — see SourcePanel's class doc).
    void showSourceStatusPanel(const QString& sourceId);
    // Shared by showSourceStatusPanel() and updateSourceAuthIndicator()'s
    // live-refresh path: paints just sourcePanel_'s auth section (prompt /
    // error+Retry / hidden) from the given state — never touches the
    // hero/capabilities, which setSource() already established once and
    // don't change afterward.
    void refreshAuthSection(const QString& sourceId, const SourceAuthState& state);

    Rpc::SourceManager& sourceManager_;
    Playback::PlaybackController& playback_;
    Config::Settings& settings_;

    void repositionTrackListBusyIndicator();
    // Shows/hides trackListPane_ and, together with it, collapses/restores
    // contentSplitter_'s sizes so heroPanel_ claims the freed width — a
    // radioStation (My Wave) selection hides the list entirely, and without
    // this heroPanel_ would just stay pinned at its persisted width instead
    // of using the freed-up space (see HeroPanel::setFillMode() — it stays
    // true regardless, this is purely about the splitter's own sizes now).
    void setTrackListVisible(bool visible);
    // Recomputes and repaints NavItemDelegate's hover state for the sidebar
    // — called from real mouse-move/leave on sidebarView_'s viewport (via
    // eventFilter()) and from its scrollbar's valueChanged, since Qt's own
    // per-row hover tracking doesn't survive a scroll (see NavItemDelegate's
    // class doc). A no-op if `index` is already the current hovered one.
    void updateSidebarHover(const QModelIndex& index);

    SidebarModel* sidebarModel_ = nullptr;
    QTreeView* sidebarView_ = nullptr;
    NavItemDelegate* sidebarDelegate_ = nullptr;
    HeroPanel* heroPanel_ = nullptr;
    TrackListModel* trackListModel_ = nullptr;
    QListView* trackListView_ = nullptr;
    QSplitter* contentSplitter_ = nullptr;
    // Thin wrapper around just trackListView_ — its own immediate parent,
    // needed so trackListBusyIndicator_'s x()/y()-based positioning (same
    // parent as trackListView_) and the resize event filter both keep
    // working when contentSplitter_ resizes this pane independently of
    // trackListContainer as a whole (see the .cpp).
    QWidget* trackListPane_ = nullptr;
    QProgressBar* trackListBusyIndicator_ = nullptr;
    CoverArtCache* coverArtCache_ = nullptr;
    TrackRowDelegate* trackRowDelegate_ = nullptr;
    NowPlayingBar* nowPlayingBar_ = nullptr;
    SourcePanel* sourcePanel_ = nullptr;
    // Shown instead of contentSplitter_/sourcePanel_ until the first
    // playlist/History/source selection — see the constructor and
    // showPlaylistAsync()/showHistory()/showSourceStatusPanel().
    EmptyStatePlaceholder* emptyStatePlaceholder_ = nullptr;
    ToastNotifier* toastNotifier_ = nullptr;
    History::PlaybackHistory* playbackHistory_ = nullptr;

    // Stashed so HeroPanel's Play button (clicked well after
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
