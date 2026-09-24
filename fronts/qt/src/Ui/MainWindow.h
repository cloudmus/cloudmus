#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>

#include <functional>

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
class PlaylistSheet;

// Sidebar + track list + persistent now-playing bar + per-source auth status
// panel + hamburger menu (Settings/About/Quit) — see the plan's UI/UX design.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(Rpc::SourceManager& sourceManager, Playback::PlaybackController& playback, Config::Settings& settings,
        QWidget* parent = nullptr);
    ~MainWindow() override;

    // Shared with integrations that show the current track's cover (the
    // desktop notification).
    CoverArtCache* coverArtCache() const { return coverArtCache_; }

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
    // Right-click on sidebarView_. A source header offers "Force Refresh
    // Playlists" (re-runs loadPlaylistsAsync); a Wave/Liked/Playlist row
    // offers "Play" (same as double-click — see onSidebarDoubleClicked).
    // Any other row (PlaylistsHeader/History) gets no menu.
    void onSidebarContextMenuRequested(const QPoint& pos);
    // The main track list mirrors the active playlist: the playback queue
    // once anything was queued, or activeTracks_ before that — see
    // refreshMainList(). A double-click jumps within it.
    void onTrackDoubleClicked(const QModelIndex& index);
    void onTrackContextMenuRequested(const QPoint& pos);
    // Shared track context menu for the main list and the sheet: Play
    // (`play`, which differs between the two)/Play Next/Add to Queue always
    // shown; Like/Dislike/Start Radio/Save to Downloads only when the
    // source declares the matching capability; Open Track Page only when
    // the track has a webUrl.
    void showTrackMenu(
        const QString& sourceId, const Track& track, const QPoint& globalPos, std::function<void()> play);
    // --- editing playlists (docs/protocol.md §7.6)
    // Fills `menu` with the user's editable playlists on the track's
    // source, each a checkbox — checked when the track is in it; toggling
    // adds/removes it. Shows "Loading…" until membership arrives; with
    // `reopenAt`, re-pops the menu there once filled (it grew, and has to
    // stay on screen). Used by the toolbar button and the context menu.
    Rpc::Task<void> fillPlaylistsMenuAsync(
        QPointer<QMenu> menu, QString sourceId, Track track, std::optional<QPoint> reopenAt = std::nullopt);
    Rpc::Task<void> setTrackInPlaylistAsync(
        QString sourceId, Track track, Playlist playlist, bool add, QPointer<QCheckBox> box);
    // Keeps what's on screen in step after a successful add/remove: the
    // sheet's list and header if it shows that playlist, the active
    // playlist's not-yet-queued tracks.
    void applyPlaylistEdit(
        const QString& sourceId, const Track& track, const QString& playlistId, bool added, int trackCount);
    // The toolbar button's menu for the playing track.
    void showPlaylistsMenu(QPoint anchor);
    bool sourceCanEditPlaylists(const QString& sourceId) const;

    // HeroPanel's Play button: (re)starts the active playlist.
    void playActive();
    void showAboutDialog();

    // --- active playlist (what the main area shows and the queue came from)
    struct ActiveContext {
        QString sourceId; // empty for History
        Playlist playlist;
        bool isHistory = false;
        // False for ad-hoc contexts (a radio started from a track) that
        // can't be restored at startup — they aren't saved to settings.
        bool persistent = true;
        bool isValid() const { return isHistory || !playlist.id.isEmpty(); }
        bool isRadio() const { return playlist.kind == QStringLiteral("radioStation"); }
        bool sameAs(const ActiveContext& other) const
        {
            return isHistory == other.isHistory && sourceId == other.sourceId && playlist.id == other.playlist.id;
        }
    };
    ActiveContext historyContext() const;
    // Makes `context` active without touching playback: sidebar marker,
    // settings, hero promo (when nothing plays), main list.
    void setActiveContext(const ActiveContext& context);
    // Makes `context` active and plays `entries` from startIndex; closes the sheet.
    void activate(const ActiveContext& context, const QVector<Playback::QueueEntry>& entries, int startIndex);
    // Fetches a playlist's tracks (Liked/regular; not radio) — throws on RPC failure.
    Rpc::Task<QVector<Playback::QueueEntry>> fetchTracksAsync(QString sourceId, Playlist playlist);
    // Sidebar double-click / context-menu Play: fetch, then activate().
    Rpc::Task<void> activateAndPlayAsync(QString sourceId, Playlist playlist);
    // Startup restore: fills activeTracks_ for the restored context.
    Rpc::Task<void> loadActiveTracksAsync(ActiveContext context);
    void refreshMainList();
    // Hero shows the playing track (trackChanged) or, with nothing
    // playing, the active playlist's promo card.
    void refreshHero();
    // Sidebar selection follows what is on screen: the sheet's playlist
    // while it's open, the active one otherwise.
    void syncSidebarSelection();

    // --- the sheet (anything that isn't the active playlist)
    Rpc::Task<void> openInSheetAsync(QString sourceId, Playlist playlist);
    void openHistoryInSheet();
    void fillHistorySheet();
    void closeSheet();
    void activateFromSheet(int row);
    void playAllFromSheet();

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
    // Starts a radio station and, once it's running, makes `context` active.
    Rpc::Task<void> startRadioAsync(QString sourceId, QString seed, ActiveContext context);
    Rpc::Task<void> submitAuthAsync(QString sourceId, QJsonObject fields);
    // The Retry button's handler: wraps ensureAuthenticatedAsync with
    // sourcePanel_'s busy state (disables Retry/Submit + shows a spinner
    // for the duration) so a click can't be repeated mid-flight and the
    // user sees something actually happened.
    Rpc::Task<void> retryAuthAsync(QString sourceId);
    // Like/dislike for an arbitrary track (the toolbar's like/dislike
    // clicks pass playback_.currentSourceId()/currentTrack().id; the track
    // list's context menu passes whatever row was right-clicked — this
    // doesn't have to be the currently-playing track). `liked`/`disliked`
    // is the requested new state (see NowPlayingBar::likeClicked/
    // dislikeClicked's doc comment for the toolbar case). On success the
    // new state goes into trackStates_, which every view reads from.
    // NowPlayingBar's busy/checked state and rollback-on-failure only
    // apply when the acted-on track is still the one it's currently
    // showing (see the stillCurrent() guard in the .cpp) — a context-menu
    // click on some other row leaves the toolbar alone. `announceSuccess`
    // shows a toast on success: the toolbar's own button already gives
    // visual confirmation for its own clicks (announceSuccess=false,
    // default), but a context-menu click has no other feedback
    // (announceSuccess=true).
    Rpc::Task<void> likeToggledAsync(QString sourceId, QString trackId, bool liked, bool announceSuccess = false);
    Rpc::Task<void> dislikeToggledAsync(QString sourceId, QString trackId, bool disliked, bool announceSuccess = false);
    // catalog.downloadTrack into settings_.downloadDirectory() — the track
    // list context menu's "Save to Downloads" (only offered when the
    // track's source declares the `download` capability).
    Rpc::Task<void> downloadTrackAsync(QString sourceId, Track track);
    // Toolbar's Save-to-Downloads button: wraps downloadTrackAsync() for
    // the currently-playing track with NowPlayingBar's busy indicator,
    // same stillCurrent()-guard shape as likeToggledAsync().
    Rpc::Task<void> downloadCurrentTrackAsync();

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
    // Entry point from onSidebarActivated: opens sourcePanel_ for this
    // source in the sheet (every source gets this, not just ones with an
    // auth problem — see SourcePanel's class doc).
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
    // Pushes trackStates_'s like/dislike for the playing track into nowPlayingBar_.
    void refreshNowPlayingFeedback();

    ActiveContext activeContext_;
    // The active playlist's tracks while nothing has been queued from it
    // yet (restored at startup) — see refreshMainList().
    QVector<Playback::QueueEntry> activeTracks_;
    // What the sheet shows (invalid while it shows a source page or is closed).
    ActiveContext sheetContext_;
    // Saved active playlist waiting for its source's playlists to load.
    Config::Settings::ActivePlaylistRef pendingRestore_;

    bool reallyQuitting_ = false;
};

} // namespace Ui
