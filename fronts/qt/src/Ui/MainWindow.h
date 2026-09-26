#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>
#include <QSet>

#include <functional>

#include "AuthStates.h"
#include "Core.h"
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
class QMenu;
class QCheckBox;

namespace History {
class PlaybackHistory;
}

namespace Library {
class TrackStates;
}

namespace Covers {
class CoverArtCache;
}

namespace ViewModel {
class SidebarModel;
}

namespace Ui {

class NavItemDelegate;
class TrackListModel;
class TrackRowDelegate;
class NowPlayingBar;
class SourcePanel;
class ToastNotifier;
class HeroPanel;
class EmptyStatePlaceholder;
class PlaylistSheet;

// Sidebar + track list + persistent now-playing bar + per-source auth status
// panel + hamburger menu (Settings/About/Quit) — see the plan's UI/UX design.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // `core`: everything the window shows and acts on — it outlives the
    // window (see App::Core).
    explicit MainWindow(App::Core& core, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Called by the tray's Quit action — bypasses close-to-tray.
    void quitForReal();

    // --- window visibility (tray, notification clicks, MPRIS Raise)
    // Whether the window can actually be seen: shown, and neither
    // minimized nor otherwise taken off screen by the compositor (a
    // minimized window on Wayland only shows as not exposed).
    bool isOnScreen() const;
    // Out of the tray, back from minimized (keeping it maximized if it
    // was) and in front of other windows. `activationToken` — the
    // compositor's permission to take focus on Wayland, when the caller
    // got one (a notification click).
    void bringToFront(const QString& activationToken = QString());
    // The tray's Show/Hide: hides the window only when it's on screen;
    // a minimized one is brought back instead.
    void toggleShown();

signals:
    void aboutToReallyQuit();

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Theme::glassChanged(): the window's translucency follows.
    void applyGlass();
    // Hooks the toolbar, the hero panel and the playing-row highlight up
    // to nowPlaying_, and shows its current state right away.
    void bindNowPlaying();

    // ViewModel::Sources' signals: the sidebar selection, the source's
    // page, and the startup restores follow.
    void onSourceChanged(const QString& sourceId);
    void onSourceRemoved(const QString& sourceId);
    void onPlaylistsLoaded(const QString& sourceId, const QList<Playlist>& playlists);
    void onSidebarActivated(const QModelIndex& index);
    void onSidebarDoubleClicked(const QModelIndex& index);
    // Right-click on sidebarView_. A source header offers "Force Refresh
    // Playlists" (ViewModel::Sources::refresh()); a Wave/Liked/Playlist row
    // offers "Play" (same as double-click — see onSidebarDoubleClicked).
    // Any other row (PlaylistsHeader/History) gets no menu.
    void onSidebarContextMenuRequested(const QPoint& pos);
    // Puts a playlist in the source's sidebar favorites or takes it out.
    void toggleFavorite(const QString& sourceId, const QString& playlistId);
    // The main track list mirrors the active playlist: the playback queue
    // once anything was queued, or its tracks before that — see
    // refreshMainList(). A double-click plays that row.
    void onTrackDoubleClicked(const QModelIndex& index);
    void onTrackContextMenuRequested(const QPoint& pos);
    // Shared track context menu for the main list and the sheet: Play
    // (`play`, which differs between the two)/Play Next/Add to Queue always
    // shown; Like/Dislike/Start Radio/Save to Downloads only when the
    // source declares the matching capability; Open Track Page only when
    // the track has a webUrl.
    void showTrackMenu(
        const QString& sourceId, const Track& track, const QPoint& globalPos, std::function<void()> play);
    // --- editing playlists (docs/protocol.md §7.6), through
    // App::PlaylistEditing
    // Fills `menu` with the user's editable playlists on the track's
    // source, each a checkbox — checked when the track is in it; toggling
    // adds/removes it. Shows "Loading…" until membership arrives; with
    // `reopenAt`, re-pops the menu there once filled (it grew, and has to
    // stay on screen). Used by the toolbar button and the context menu.
    Rpc::Task<void> fillPlaylistsMenuAsync(
        QPointer<QMenu> menu, QString sourceId, Track track, std::optional<QPoint> reopenAt = std::nullopt);
    // A playlist's check box toggled: `box` is disabled while the request
    // runs and unchecked back on failure.
    Rpc::Task<void> setTrackInPlaylistAsync(
        QString sourceId, Track track, Playlist playlist, bool add, QPointer<QCheckBox> box);
    // Keeps what's on screen in step after a successful add/remove — from
    // here or the tray (App::PlaylistEditing::playlistEdited): the
    // sidebar's count, the sheet's list and header if it shows that
    // playlist, the active playlist's not-yet-queued tracks.
    void applyPlaylistEdit(
        const QString& sourceId, const Track& track, const QString& playlistId, bool added, int trackCount);
    // The toolbar button's menu for the playing track.
    void showPlaylistsMenu(QPoint anchor);

    void showAboutDialog();
    // `openAt`: a Settings::Page::id() to open the dialog on.
    void showSettingsDialog(const QString& openAt = { });

    // --- active playlist: ViewModel::ActivePlaylist; the sheet's open
    // playlist is one of these too.
    using ActiveContext = ViewModel::PlaylistContext;
    // Hooks the main list, the hero's playlist mode and the busy states up
    // to activePlaylist_, and shows what it has right away.
    void bindActivePlaylist();
    // The main list shows ActivePlaylist::entries().
    void refreshMainList();
    // Hero shows the playing track (trackChanged) or, with nothing
    // playing, the active playlist's promo card.
    void refreshHero();
    // Sidebar selection follows what is on screen: the sheet's playlist
    // while it's open, the active one otherwise.
    void syncSidebarSelection();
    // Expands the rows just inserted (and their children), except the ones
    // the user collapsed — see ViewModel::Sources::isCollapsed().
    void restoreExpansion(const QModelIndex& parent, int first, int last);
    // Names what syncSidebarSelection() selects — "history",
    // "source:<id>" or "playlist:<sourceId>:<playlistId>" — or empty.
    QString selectionKey() const;
    // Opens the page the sidebar had selected when the app last quit, once
    // `sourceId` has listed its playlists (empty: History, available at once).
    void restoreSelection(const QString& sourceId, const QList<Playlist>& playlists);

    // --- the sheet (anything that isn't the active playlist)
    Rpc::Task<void> openInSheetAsync(QString sourceId, Playlist playlist);
    void openHistoryInSheet();
    void fillHistorySheet();
    void closeSheet();
    void activateFromSheet(int row);
    void playAllFromSheet();

    // By value, not const&: these coroutines resume asynchronously (after an
    // RPC round-trip) and use their params again after that resume — a
    // reference to a caller's temporary/local (as with detach()ed calls
    // from onSidebarActivated) would dangle by the time execution gets back
    // there. See the equivalent comment on RpcClient::call() for the full
    // explanation of this coroutine-lifetime pitfall.
    Rpc::Task<void> submitAuthAsync(QString sourceId, QJsonObject fields);
    // The Retry button's handler: wraps App::SourceSession::signIn() with
    // sourcePanel_'s busy state (disables Retry/Submit + shows a spinner
    // for the duration) so a click can't be repeated mid-flight and the
    // user sees something actually happened.
    Rpc::Task<void> retryAuthAsync(QString sourceId);

    // Per-source sign-in state, shared with the Settings dialog's source
    // pages — see Rpc::AuthStates. Feeds both the sidebar's warning icon
    // (always) and sourcePanel_'s auth section (when it's showing that
    // source), via updateSourceAuthIndicator() on its changed(). Owned by
    // App::Core, like the other services below.
    Rpc::AuthStates* authStates_ = nullptr;
    // sourceId sourcePanel_ is currently showing, or empty if it's hidden /
    // a normal playlist is showing instead.
    QString currentStatusPanelSourceId_;

    // Updates the sidebar icon for sourceId from sourceAuthStates_, and — if
    // sourcePanel_ is currently showing exactly this source — its auth
    // section too, so a prompt/status update arriving while the panel is
    // already open refreshes it live instead of needing a re-click.
    void updateSourceAuthIndicator(const QString& sourceId);
    // Entry point from onSidebarActivated: opens sourcePanel_ for this
    // source in the sheet (every source gets this, not just ones with an
    // auth problem — see SourcePanel's class doc).
    void showSourceStatusPanel(const QString& sourceId);
    // Shared by showSourceStatusPanel() and updateSourceAuthIndicator()'s
    // live-refresh path: paints just sourcePanel_'s auth section (prompt /
    // error+Retry / hidden) from the given state — never touches the
    // hero/capabilities, which setSource() already established once and
    // don't change afterward.
    void refreshAuthSection(const QString& sourceId, const Rpc::AuthStates::State& state);

    Rpc::SourceManager& sourceManager_;
    Playback::PlaybackController& playback_;
    Config::Settings& settings_;
    App::SourceSession& sourceSession_;
    // What to tell the user goes here; posted() shows it as a toast on
    // toastNotifier_ (redirected while the Settings dialog is up).
    ViewModel::Messages& messages_;
    // What's playing — the toolbar, the hero panel and the playing-row
    // highlight follow it (bindNowPlaying()).
    ViewModel::NowPlaying& nowPlaying_;
    App::PlaylistEditing& playlistEditing_;
    // The sources and their playlists; owns sidebarModel_.
    ViewModel::Sources& sources_;
    // What the main area has active.
    ViewModel::ActivePlaylist& activePlaylist_;

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

    ViewModel::SidebarModel* sidebarModel_ = nullptr;
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
    Covers::CoverArtCache* coverArtCache_ = nullptr;
    TrackRowDelegate* trackRowDelegate_ = nullptr;
    NowPlayingBar* nowPlayingBar_ = nullptr;
    // Lives inside sheet_ (its source page).
    SourcePanel* sourcePanel_ = nullptr;
    // Shown instead of contentSplitter_ while there's no active playlist
    // at all (first run) — see refreshMainList().
    EmptyStatePlaceholder* emptyStatePlaceholder_ = nullptr;
    QWidget* trackListContainer_ = nullptr;
    PlaylistSheet* sheet_ = nullptr;
    ToastNotifier* toastNotifier_ = nullptr;
    History::PlaybackHistory* playbackHistory_ = nullptr;
    // Like/dislike/last-played per track, shared by every view — see
    // Library::TrackStates.
    Library::TrackStates* trackStates_ = nullptr;
    // Into the tray, remembering where the window was for bringToFront().
    void hideToTray();

    // What the sheet shows (invalid while it shows a source page or is closed).
    ActiveContext sheetContext_;
    // Settings::sidebarSelection() from the last run, until restored or
    // replaced by a new selection.
    QString pendingSelection_;

    bool reallyQuitting_ = false;
    // saveGeometry() taken by hideToTray(); empty while the window is shown.
    QByteArray trayHiddenGeometry_;
};

} // namespace Ui
