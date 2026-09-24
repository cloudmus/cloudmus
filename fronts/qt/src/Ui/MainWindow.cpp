#include "MainWindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QCursor>
#include <QDesktopServices>
#include <QEvent>
#include <QListView>
#include <QLoggingCategory>
#include <QMenu>
#include <QMouseEvent>
#include <QProgressBar>
#include <QScrollBar>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include "AboutDialog.h"
#include "CoverArtCache.h"
#include "EmptyStatePlaceholder.h"
#include "HeroPanel.h"
#include "Icons.h"
#include "Metrics.h"
#include "NavItemDelegate.h"
#include "NowPlayingBar.h"
#include "OverlayScrollBar.h"
#include "PlaybackHistory.h"
#include "PlaylistSheet.h"
#include "RpcMethods.h"
#include "ScrollEdgeFade.h"
#include "SettingsDialog.h"
#include "SidebarModel.h"
#include "SmoothScroller.h"
#include "SourcePanel.h"
#include "Spacing.h"
#include "ToastNotifier.h"
#include "Tokens.h"
#include "TrackHoverCard.h"
#include "TrackListModel.h"
#include "TrackRowDelegate.h"
#include "TrackStates.h"

namespace Ui {

namespace {
// Warning-level, so it always shows regardless of --debug/CLOUDMUS_QT_DEBUG
// (see Logging.cpp's messageHandler) — every RPC/async failure this window
// surfaces to the user via a toast also gets logged here, so a report like
// "I clicked X and nothing happened" has something to look at in the
// console even if the toast was missed.
Q_LOGGING_CATEGORY(lcMainWindow, "cloudmus.ui.mainwindow")
} // namespace

MainWindow::MainWindow(Rpc::SourceManager& sourceManager, Playback::PlaybackController& playback,
    Config::Settings& settings, QWidget* parent)
    : QMainWindow(parent)
    , sourceManager_(sourceManager)
    , playback_(playback)
    , settings_(settings)
{
    setWindowTitle(QStringLiteral("CloudMus"));
    resize(960, 640);
    restoreGeometry(settings_.windowGeometry());
    // QMainWindow's default behavior: right-clicking a toolbar/dock area
    // pops up a menu of toggle-visibility checkboxes for every toolbar
    // (createPopupMenu()). With only one toolbar here (the transport bar —
    // play/pause, like/dislike, downloads, the hamburger menu itself) and
    // no menu bar to bring it back from, an accidental click there would
    // hide the only way to control playback at all. Disabling it entirely
    // is Qt's own documented fix (QMainWindow::setContextMenuPolicy docs).
    setContextMenuPolicy(Qt::NoContextMenu);

    coverArtCache_ = new CoverArtCache(this);
    playbackHistory_ = new History::PlaybackHistory(this);
    trackStates_ = new Library::TrackStates(this);
    // History's saved snapshots: when each track was last played, plus
    // whatever like state it was recorded with — only where nothing
    // fresher is known (a source's own report always wins, see observe()).
    for (const History::HistoryEntry& e : playbackHistory_->entries()) {
        trackStates_->setLastPlayed(e.sourceId, e.track.id, e.playedAt);
        trackStates_->observe(e.sourceId, e.track, /*onlyIfUnknown=*/true);
    }
    connect(
        trackStates_, &Library::TrackStates::changed, this, [this](const QString& sourceId, const QString& trackId) {
            if (playback_.hasCurrentTrack() && playback_.currentSourceId() == sourceId
                && playback_.currentTrack().id == trackId)
                refreshNowPlayingFeedback();
        });
    connect(trackStates_, &Library::TrackStates::bulkChanged, this, &MainWindow::refreshNowPlayingFeedback);

    // --- now-playing controls, merged into the bottom toolbar alongside
    // the hamburger menu. No cover art here — HeroPanel (below) shows
    // whatever's playing instead, so it isn't duplicated. ---
    nowPlayingBar_ = new NowPlayingBar(this);
    // setVolume() alone only moves the slider — it's built on a
    // QSignalBlocker specifically so restoring the persisted position at
    // startup doesn't loop back through volumeChanged (see its .cpp). That
    // means it never actually reaches AudioPlayer, which otherwise starts
    // at mpv's own default (max) until the user first drags the slider —
    // apply the persisted value to playback_ explicitly here too.
    nowPlayingBar_->setVolume(settings_.volume());
    playback_.setVolume(settings_.volume());
    connect(nowPlayingBar_, &NowPlayingBar::playPauseClicked, &playback_, &Playback::PlaybackController::togglePause);
    connect(nowPlayingBar_, &NowPlayingBar::nextClicked, &playback_, &Playback::PlaybackController::next);
    connect(nowPlayingBar_, &NowPlayingBar::previousClicked, &playback_, &Playback::PlaybackController::previous);
    connect(nowPlayingBar_, &NowPlayingBar::stopClicked, &playback_, &Playback::PlaybackController::stop);
    connect(nowPlayingBar_, &NowPlayingBar::seekRequested, &playback_, &Playback::PlaybackController::seek);
    connect(nowPlayingBar_, &NowPlayingBar::volumeChanged, this, [this](int v) {
        playback_.setVolume(v);
        settings_.setVolume(v);
    });
    connect(nowPlayingBar_, &NowPlayingBar::likeClicked, this, [this](bool liked) {
        if (!playback_.hasCurrentTrack())
            return;
        likeToggledAsync(playback_.currentSourceId(), playback_.currentTrack().id, liked).detach();
    });
    connect(nowPlayingBar_, &NowPlayingBar::dislikeClicked, this, [this](bool disliked) {
        if (!playback_.hasCurrentTrack())
            return;
        dislikeToggledAsync(playback_.currentSourceId(), playback_.currentTrack().id, disliked).detach();
    });
    connect(nowPlayingBar_, &NowPlayingBar::downloadClicked, this, [this]() { downloadCurrentTrackAsync().detach(); });

    // The single declarative source of truth for every control's enabled
    // state and value — see NowPlayingBar::setTrackAvailable()'s doc
    // comment. Also where Stop's "reset to undefined" becomes visible:
    // once hasCurrentTrack() goes false, heroPanel_ reverts to promoting
    // the browsed playlist/cover art and the track list's
    // currently-playing row clears, instead of leaving the last-played
    // track's info on screen.
    connect(&playback_, &Playback::PlaybackController::currentTrackAvailabilityChanged, this, [this](bool available) {
        nowPlayingBar_->setTrackAvailable(available);
        if (!available) {
            nowPlayingBar_->setTrackWebUrl(QString());
            nowPlayingBar_->setLikeState(false, false);
            nowPlayingBar_->setDislikeState(false, false);
            nowPlayingBar_->setDownloadState(false);
            refreshHero();
            trackRowDelegate_->setCurrentlyPlaying(QString(), QString());
            trackListView_->viewport()->update();
            sheet_->trackDelegate()->setCurrentlyPlaying(QString(), QString());
            sheet_->updateRows();
        }
    });
    connect(&playback_, &Playback::PlaybackController::queueChanged, this, &MainWindow::refreshMainList);
    connect(&playback_, &Playback::PlaybackController::queueAvailabilityChanged, this,
        [this](bool available) { nowPlayingBar_->setQueueAvailable(available); });

    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString& sourceId) {
            playbackHistory_->record(sourceId, track);
            trackStates_->setLastPlayed(sourceId, track.id, QDateTime::currentDateTimeUtc());
        });
    connect(&playback_, &Playback::PlaybackController::trackChanged, this, [this](const Track& track, const QString&) {
        nowPlayingBar_->setTrackWebUrl(track.webUrl.value_or(QString()));
    });
    connect(
        &playback_, &Playback::PlaybackController::trackChanged, this, [this](const Track&, const QString& sourceId) {
            const Rpc::RpcClient* client = sourceManager_.client(sourceId);
            const QJsonObject capabilities = client != nullptr ? client->capabilities() : QJsonObject();
            refreshNowPlayingFeedback();
            nowPlayingBar_->setDownloadState(capabilities.value(QStringLiteral("download")).toBool());
        });
    // HeroPanel shows what's playing instead of the active playlist's
    // promo card whenever playback_.hasCurrentTrack() — see refreshHero(),
    // and the currentTrackAvailabilityChanged handler above for how Stop
    // reverts it.
    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString&) { heroPanel_->setNowPlaying(track); });
    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString& sourceId) {
            trackRowDelegate_->setCurrentlyPlaying(sourceId, track.id);
            trackListView_->viewport()->update();
            sheet_->trackDelegate()->setCurrentlyPlaying(sourceId, track.id);
            sheet_->updateRows();
        });
    connect(playbackHistory_, &History::PlaybackHistory::changed, this, [this]() {
        // Refresh in place — a track just started playing.
        if (sheetContext_.isHistory && sheet_->isPresented())
            fillHistorySheet();
    });
    connect(&playback_, &Playback::PlaybackController::playingChanged, nowPlayingBar_, &NowPlayingBar::setPlaying);
    connect(&playback_, &Playback::PlaybackController::loadingChanged, nowPlayingBar_, &NowPlayingBar::setLoading);
    connect(&playback_, &Playback::PlaybackController::positionChanged, nowPlayingBar_, &NowPlayingBar::setPosition);

    auto* toolbar = new QToolBar(this);
    toolbar->setObjectName(QStringLiteral("transportToolBar")); // see StyleSheet.cpp's toolBarBlock()
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setAllowedAreas(Qt::BottomToolBarArea);
    toolbar->addWidget(nowPlayingBar_);
    // Into NowPlayingBar's own transport-button row (top row, right end),
    // not a separate toolbar item — see setTrailingWidget()'s comment for
    // why the menu itself is still built here rather than in that class.
    auto* menuButton = new QToolButton(nowPlayingBar_);
    // Theme::Metrics::iconButtonSize/iconGlyphSize to match NowPlayingBar's
    // IconHoverButton transport controls beside it — same Icon Button
    // treatment (surface-200 circle at rest, surface-300/400 hover/pressed)
    // via Theme::StyleSheet's QPushButton[variant="icon"] rule, which also
    // matches QToolButton, so it doesn't stand out as a native square
    // button next to the round transport row.
    menuButton->setIcon(
        Theme::icon(QStringLiteral("menu"), Theme::IconColor::InkSecondary, Theme::Metrics::iconGlyphSize));
    menuButton->setProperty("variant", "icon");
    menuButton->setFixedSize(Theme::Metrics::iconButtonSize, Theme::Metrics::iconButtonSize);
    menuButton->setIconSize(QSize(Theme::Metrics::iconGlyphSize, Theme::Metrics::iconGlyphSize));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(menuButton);
    menu->addAction(tr("Settings…"), this, [this]() {
        SettingsDialog dialog(settings_, this);
        dialog.exec();
    });
    menu->addAction(tr("About CloudMus"), this, &MainWindow::showAboutDialog);
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, &MainWindow::quitForReal);
    menuButton->setMenu(menu);
    nowPlayingBar_->setTrailingWidget(menuButton);
    addToolBar(Qt::BottomToolBarArea, toolbar);

    // --- sidebar + track list ---
    sidebarModel_ = new SidebarModel(this);
    sidebarModel_->ensureHistoryItem();
    sidebarView_ = new QTreeView(this);
    sidebarView_->setObjectName(QStringLiteral("sidebarView")); // see StyleSheet.cpp's sidebarTreeBlock()
    sidebarView_->setModel(sidebarModel_);
    sidebarDelegate_ = new NavItemDelegate(sidebarView_);
    sidebarView_->setItemDelegate(sidebarDelegate_);
    sidebarView_->setHeaderHidden(true);
    // Items are QStandardItems, editable by default — without this, the
    // double-click wired below to start playback also opens a rename
    // editor on the row.
    sidebarView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Indent step per nesting level (source -> section -> playlist), per
    // the design system's NavItem spec — the branch arrow's own color now
    // comes from Theme::StyleSheet's QTreeView::branch image rules instead
    // of the style's native rendering.
    sidebarView_->setIndentation(Theme::Spacing::space5);
    sidebarView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    SmoothScroller::attach(sidebarView_);
    // Before OverlayScrollBar::attach — see ScrollEdgeFade's class doc.
    ScrollEdgeFade::attach(sidebarView_, [] { return Theme::palette().surface100; });
    OverlayScrollBar::attach(sidebarView_);
    // Real mouse-move events over the viewport drive NavItemDelegate's hover
    // via eventFilter() below — needs mouse tracking on to get them without
    // a button held. Qt's own per-row State_MouseOver isn't used at all
    // (see NavItemDelegate's class doc) specifically because it doesn't
    // survive a scroll: dragging the scrollbar slides row content under a
    // stationary cursor without producing a move event, so the scrollbar's
    // valueChanged below recomputes hover the same way, straight from
    // indexAt() against the cursor's current position — not by trying to
    // synthesize whatever event Qt would otherwise have delivered.
    sidebarView_->viewport()->setMouseTracking(true);
    sidebarView_->viewport()->installEventFilter(this);
    connect(sidebarView_->verticalScrollBar(), &QScrollBar::valueChanged, sidebarView_, [this]() {
        const QPoint viewportPos = sidebarView_->viewport()->mapFromGlobal(QCursor::pos());
        const bool inside = sidebarView_->viewport()->rect().contains(viewportPos);
        updateSidebarHover(inside ? sidebarView_->indexAt(viewportPos) : QModelIndex());
    });
    connect(sidebarModel_, &QStandardItemModel::rowsInserted, sidebarView_, &QTreeView::expandAll);
    connect(sidebarView_, &QTreeView::clicked, this, &MainWindow::onSidebarActivated);
    connect(sidebarView_, &QTreeView::doubleClicked, this, &MainWindow::onSidebarDoubleClicked);
    sidebarView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(sidebarView_, &QTreeView::customContextMenuRequested, this, &MainWindow::onSidebarContextMenuRequested);

    // Every discovered backend gets its header row up front, before its
    // process has even been spawned (sourceManager_.startAll() runs after
    // MainWindow is constructed — see main.cpp) — a backend that's slow to
    // start or still authenticating must stay visible instead of only
    // appearing once loadPlaylistsAsync() below first succeeds. Shown as
    // loading until wireSource()'s loadPlaylistsAsync() call fills it in
    // for real.
    for (const auto& manifest : Rpc::discoverManifests()) {
        sidebarModel_->setSourceIconPath(manifest.id, manifest.iconPath);
        sidebarModel_->setSource(manifest.id, manifest.name, { });
        sidebarModel_->setSourceLoading(manifest.id, manifest.name, true);
    }

    heroPanel_ = new HeroPanel(coverArtCache_, this);
    // Always the tall full-height splitter pane below, regardless of
    // whether trackListPane_ is currently visible — unlike the old
    // PlaylistHeader, this is never toggled again after construction (see
    // setTrackListVisible()).
    heroPanel_->setFillMode(true);
    connect(heroPanel_, &HeroPanel::playClicked, this, &MainWindow::playActive);

    trackListModel_ = new TrackListModel(this);
    trackListModel_->setTrackStates(trackStates_);
    trackRowDelegate_ = new TrackRowDelegate(coverArtCache_, this);
    connect(coverArtCache_, &CoverArtCache::pixmapReady, this, [this]() { trackListView_->viewport()->update(); });
    trackListView_ = new QListView(this);
    trackListView_->setObjectName(QStringLiteral("trackListView")); // see StyleSheet.cpp's trackListBlock()
    trackListView_->setModel(trackListModel_);
    trackListView_->setItemDelegate(trackRowDelegate_);
    // Needed for State_MouseOver to be set at all — see the delegate's
    // hover-only play button drawn over the cover thumbnail.
    trackListView_->setMouseTracking(true);
    trackListView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    SmoothScroller::attach(trackListView_);
    ScrollEdgeFade::attach(trackListView_, [] { return Theme::palette().surface0; });
    OverlayScrollBar::attach(trackListView_);
    connect(trackListView_, &QListView::doubleClicked, this, &MainWindow::onTrackDoubleClicked);
    connect(trackRowDelegate_, &TrackRowDelegate::playRequested, this, &MainWindow::onTrackDoubleClicked);
    trackListView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(trackListView_, &QListView::customContextMenuRequested, this, &MainWindow::onTrackContextMenuRequested);

    auto* trackListContainer = new QWidget(this);
    trackListContainer_ = trackListContainer;
    auto* trackListContainerLayout = new QVBoxLayout(trackListContainer);
    trackListContainerLayout->setContentsMargins(0, 0, 0, 0);
    trackListContainerLayout->setSpacing(0);

    // trackListPane_ is trackListView_'s own immediate parent (rather than
    // adding trackListView_ straight into contentSplitter_) purely so
    // trackListBusyIndicator_ below keeps a same-parent x()/y() to position
    // itself against.
    trackListPane_ = new QWidget(this);
    auto* trackListPaneLayout = new QVBoxLayout(trackListPane_);
    trackListPaneLayout->setContentsMargins(0, 0, 0, 0);
    trackListPaneLayout->setSpacing(0);
    trackListPaneLayout->addWidget(trackListView_);

    contentSplitter_ = new QSplitter(Qt::Horizontal, trackListContainer);
    contentSplitter_->setProperty("themed", true); // 1px line handle — see Theme::CloudMusStyle
    contentSplitter_->addWidget(heroPanel_);
    contentSplitter_->addWidget(trackListPane_);
    contentSplitter_->setSizes({ settings_.heroPanelWidth(), 290 });
    connect(contentSplitter_, &QSplitter::splitterMoved, this,
        [this]() { settings_.setHeroPanelWidth(contentSplitter_->sizes().first()); });
    trackListContainerLayout->addWidget(contentSplitter_, 1);

    // Shown instead of contentSplitter_ until there's an active playlist
    // (restored or first played) — see refreshMainList().
    emptyStatePlaceholder_ = new EmptyStatePlaceholder(trackListContainer);
    trackListContainerLayout->addWidget(emptyStatePlaceholder_, 1);
    contentSplitter_->hide();

    // Not in the layout: covers the whole container (hero + active list)
    // when presented, kept filling it by eventFilter()'s resize handling.
    sheet_ = new PlaylistSheet(coverArtCache_, trackListContainer);
    sheet_->trackModel()->setTrackStates(trackStates_);
    const auto sourceName = [this](const QString& sourceId) {
        const Rpc::RpcClient* client = sourceManager_.client(sourceId);
        return client != nullptr ? client->sourceName() : QString();
    };
    TrackHoverCard::attach(trackListView_, coverArtCache_, sourceName);
    TrackHoverCard::attach(sheet_->trackView(), coverArtCache_, sourceName);
    trackListContainer->installEventFilter(this);
    sourcePanel_ = sheet_->sourcePanel();
    connect(sourcePanel_, &SourcePanel::submitRequested, this,
        [this](const QString& sourceId, const QJsonObject& fields) { submitAuthAsync(sourceId, fields).detach(); });
    connect(sourcePanel_, &SourcePanel::retryRequested, this,
        [this](const QString& sourceId) { retryAuthAsync(sourceId).detach(); });
    connect(sourcePanel_, &SourcePanel::playlistActivated, this,
        [this](const QString& sourceId, const Playlist& playlist) { openInSheetAsync(sourceId, playlist).detach(); });
    connect(sourcePanel_, &SourcePanel::refreshRequested, this, [this](const QString& sourceId) {
        if (Rpc::RpcClient* client = sourceManager_.client(sourceId))
            loadPlaylistsAsync(client).detach();
    });
    connect(sheet_, &PlaylistSheet::trackActivated, this, &MainWindow::activateFromSheet);
    connect(sheet_, &PlaylistSheet::playAllClicked, this, &MainWindow::playAllFromSheet);
    connect(sheet_, &PlaylistSheet::closeRequested, this, &MainWindow::closeSheet);
    connect(sheet_, &PlaylistSheet::trackContextMenuRequested, this, [this](int row, const QPoint& globalPos) {
        TrackListModel* model = sheet_->trackModel();
        showTrackMenu(
            model->sourceIdAt(row), model->trackAt(row), globalPos, [this, row]() { activateFromSheet(row); });
    });
    connect(sheet_, &PlaylistSheet::dismissed, this, [this]() {
        sheetContext_ = ActiveContext();
        currentStatusPanelSourceId_.clear();
        syncSidebarSelection();
    });

    // Not part of trackListPaneLayout: a layout-managed progress bar would
    // shrink the list by its own height whenever it's shown/hidden,
    // shoving the whole view down. It's a free-floating child positioned
    // absolutely over trackListView_'s top edge instead — see
    // eventFilter()/repositionTrackListBusyIndicator(). Parented (and
    // event-filtered) on trackListPane_, not trackListContainer: dragging
    // contentSplitter_'s handle resizes trackListPane_ directly without
    // necessarily resizing trackListContainer, so watching the outer
    // container would silently stop tracking this indicator's position
    // during a splitter drag.
    trackListBusyIndicator_ = new QProgressBar(trackListPane_);
    trackListBusyIndicator_->setProperty("themed", true); // see StyleSheet.cpp's progressBarBlock()
    trackListBusyIndicator_->setRange(0, 0);
    trackListBusyIndicator_->setMaximumHeight(4);
    trackListBusyIndicator_->setTextVisible(false);
    trackListBusyIndicator_->hide();
    trackListPane_->installEventFilter(this);

    auto* splitter = new QSplitter(this);
    splitter->setProperty("themed", true);
    splitter->addWidget(sidebarView_);
    splitter->addWidget(trackListContainer);
    splitter->setSizes({ settings_.sidebarWidth(), 720 });
    connect(splitter, &QSplitter::splitterMoved, this,
        [this, splitter]() { settings_.setSidebarWidth(splitter->sizes().first()); });

    toastNotifier_ = new ToastNotifier(this);
    connect(&playback_, &Playback::PlaybackController::errorOccurred, this,
        [this](const QString& message) { toastNotifier_->showError(message); });

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(splitter, 1);
    setCentralWidget(central);

    connect(&sourceManager_, &Rpc::SourceManager::sourceReady, this, &MainWindow::wireSource);
    connect(&sourceManager_, &Rpc::SourceManager::sourceUnavailable, this, &MainWindow::onSourceUnavailable);

    // Restore the last active playlist (without playing it). History is
    // local, so it's restored right away; a backend playlist has to wait
    // for that backend's playlists — see loadPlaylistsAsync().
    const Config::Settings::ActivePlaylistRef saved = settings_.lastActivePlaylist();
    if (saved.kind == QStringLiteral("history")) {
        setActiveContext(historyContext());
        loadActiveTracksAsync(activeContext_).detach();
    } else if (!saved.sourceId.isEmpty() && !saved.playlistId.isEmpty()) {
        pendingRestore_ = saved;
    }
}

MainWindow::~MainWindow() { settings_.setWindowGeometry(saveGeometry()); }

void MainWindow::wireSource(Rpc::RpcClient* client)
{
    client->notifications.onTrackStreamReady
        = [this, client](const StreamReadyParams& p) { playback_.handleStreamReady(client->sourceId(), p); };
    client->notifications.onRadioTracksAdded = [this, client](const TracksAddedParams& p) {
        trackStates_->observe(client->sourceId(), p.tracks);
        playback_.handleTracksAdded(client->sourceId(), p);
    };
    client->notifications.onError = [this](const ErrorParams& e) { toastNotifier_->showError(e.message); };
    client->onAuthPromptRaw = [this, client](const QJsonObject& params) {
        SourceAuthState& state = sourceAuthStates_[client->sourceId()];
        state.hasProblem = true;
        state.prompt = params;
        state.errorMessage.clear();
        updateSourceAuthIndicator(client->sourceId());
    };
    client->notifications.onAuthStatusChanged = [this, client](const StatusChangedParams& status) {
        SourceAuthState& state = sourceAuthStates_[client->sourceId()];
        if (status.status == QStringLiteral("authenticated")) {
            state.hasProblem = false;
            state.prompt = QJsonObject();
            state.errorMessage.clear();
            updateSourceAuthIndicator(client->sourceId());
            loadPlaylistsAsync(client).detach();
        } else {
            state.hasProblem = true;
            state.prompt = QJsonObject(); // an error supersedes any earlier prompt
            state.errorMessage = status.message.value_or(QString());
            qCWarning(lcMainWindow) << "auth error for" << client->sourceId() << ":" << state.errorMessage;
            toastNotifier_->showError(tr("%1: %2").arg(client->sourceName(), state.errorMessage));
            updateSourceAuthIndicator(client->sourceId());
        }
    };

    const QJsonObject auth = client->capabilities().value(QStringLiteral("auth")).toObject();
    if (auth.value(QStringLiteral("required")).toBool()) {
        ensureAuthenticatedAsync(client).detach();
    }
    loadPlaylistsAsync(client).detach();
}

Rpc::Task<void> MainWindow::ensureAuthenticatedAsync(Rpc::RpcClient* client)
{
    try {
        GetStatusResult status = co_await Rpc::authGetStatus(*client);
        if (status.status != QStringLiteral("authenticated")) {
            // auth.start is idempotent on the backend side (a session
            // already in flight just no-ops) — safe to call unconditionally
            // for unauthenticated/pending/error status alike, same as the
            // TUI's `if status["status"] != "authenticated": auth.start`.
            co_await Rpc::authStart(*client);
        }
    } catch (const std::exception& e) {
        // std::exception, not Rpc::RpcCallException: also catches
        // Rpc::ProtocolParseError (a well-formed JSON-RPC response whose
        // *content* doesn't match the protocol schema — e.g. a field typed
        // wrong) — same std::runtime_error base, e.what() carries the same
        // message either way (RpcCallException's constructor sets it from
        // error.message directly). A failure here is auth.getStatus/
        // auth.start itself erroring, distinct from the backend's own auth
        // flow later failing asynchronously via auth/statusChanged (handled
        // in wireSource's onAuthStatusChanged). Both must be visible: this
        // used to only catch RpcCallException and silently swallow anything
        // else, which is exactly how a Retry click could look like it did
        // nothing.
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcMainWindow) << "auth.getStatus/auth.start failed for" << client->sourceId() << ":" << message;
        toastNotifier_->showError(tr("%1: %2").arg(client->sourceName(), message));
    }
}

Rpc::Task<void> MainWindow::retryAuthAsync(QString sourceId)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    sourcePanel_->setAuthActionBusy(true);
    co_await ensureAuthenticatedAsync(client);
    // Guard: the user may have switched to a different source's panel (or
    // closed this one) while the round-trip was in flight — don't touch a
    // busy indicator that isn't even showing for sourceId anymore.
    if (currentStatusPanelSourceId_ == sourceId)
        sourcePanel_->setAuthActionBusy(false);
}

void MainWindow::updateSourceAuthIndicator(const QString& sourceId)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    const QString sourceName = client != nullptr ? client->sourceName() : sourceId;
    const SourceAuthState state = sourceAuthStates_.value(sourceId);
    sidebarModel_->setSourceAuthProblem(sourceId, sourceName, state.hasProblem);

    if (currentStatusPanelSourceId_ == sourceId)
        refreshAuthSection(sourceId, state);
}

void MainWindow::showSourceStatusPanel(const QString& sourceId)
{
    sheetContext_ = ActiveContext();
    currentStatusPanelSourceId_ = sourceId;

    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    const QString sourceName = client != nullptr ? client->sourceName() : sourceId;
    const QString description = client != nullptr ? client->sourceDescription() : QString();
    const QJsonObject capabilities = client != nullptr ? client->capabilities() : QJsonObject();
    sheet_->showSource(sourceName, description, sidebarModel_->sourceIconPath(sourceId));
    sourcePanel_->setSource(sourceId, capabilities);
    sourcePanel_->setPlaylists(sidebarModel_->playlistsFor(sourceId), sidebarModel_->isSourceLoading(sourceId));

    refreshAuthSection(sourceId, sourceAuthStates_.value(sourceId));
    // Last: presenting snapshots the sheet, so fill it first.
    sheet_->present();
}

void MainWindow::refreshAuthSection(const QString& sourceId, const SourceAuthState& state)
{
    Q_UNUSED(sourceId);
    if (!state.prompt.isEmpty()) {
        sourcePanel_->showPrompt(state.prompt);
    } else if (!state.errorMessage.isEmpty()) {
        sourcePanel_->showError(state.errorMessage);
    } else {
        // Authenticated, or auth not required at all — nothing to act on.
        sourcePanel_->clearAuthSection();
    }
}

void MainWindow::onSourceUnavailable(const QString& manifestId, const QString& name, QStringList stderrTail)
{
    Q_UNUSED(stderrTail);
    sidebarModel_->removeSource(manifestId);
    syncSidebarSelection();
    toastNotifier_->showError(tr("%1 is unavailable").arg(name));
}

Rpc::Task<void> MainWindow::loadPlaylistsAsync(Rpc::RpcClient* client)
{
    sidebarModel_->setSourceLoading(client->sourceId(), client->sourceName(), true);
    if (currentStatusPanelSourceId_ == client->sourceId())
        sourcePanel_->setPlaylists(sidebarModel_->playlistsFor(client->sourceId()), /*loading=*/true);
    const QJsonObject browse = client->capabilities().value(QStringLiteral("browse")).toObject();
    const bool shouldFetch = browse.value(QStringLiteral("playlists")).toBool()
        || browse.value(QStringLiteral("likedTracks")).toBool() || browse.value(QStringLiteral("radio")).toBool();
    QList<Playlist> playlists;
    // Set when the fetch failed with a client-side timeout (RpcClient's own
    // local deadline, error code -1 — see RpcClient::registerPending) while
    // otherwise looking fine — worth surfacing, unlike the routine
    // "not-yet-authenticated" rejection below, which always fails fast with
    // a proper error object rather than by timing out, so it can never hit
    // this branch.
    bool fetchTimedOut = false;
    if (shouldFetch) {
        try {
            ListPlaylistsResult result = co_await Rpc::catalogListPlaylists(*client);
            playlists = result.playlists;
        } catch (const Rpc::RpcCallException& e) {
            fetchTimedOut = e.error().code == -1;
            qCWarning(lcMainWindow) << "catalog.listPlaylists failed for" << client->sourceId() << ":" << e.what();
        } catch (const std::exception& e) {
            // std::exception, not Rpc::RpcCallException — critically also
            // catches Rpc::ProtocolParseError (a well-formed response whose
            // *content* violates the protocol schema, e.g. a field of the
            // wrong JSON type). That distinction is exactly what caused a
            // real bug: a backend returning Playlist.trackCount as a JSON
            // string for some entries threw ProtocolParseError, which this
            // catch didn't match, so the exception propagated straight out
            // of this .detach()'d coroutine (silently discarded — see
            // Coro.h's promise_type::unhandled_exception) and skipped the
            // sidebarModel_->setSource() call below entirely — the source
            // never appeared as a sidebar row at all, not just missing its
            // playlists.
            //
            // No toast here — this runs automatically (not from a button)
            // and RpcCallException specifically fires routinely for every
            // not-yet-authenticated source at startup, which isn't worth
            // interrupting the user for. But any failure here can also mean
            // a real upstream problem for a source that IS authenticated,
            // which used to be entirely invisible — console log it either
            // way so that case is at least diagnosable without re-running
            // the backend by hand. The sidebar itself simply won't show
            // playlists for this source until it retries (e.g. after auth
            // completes, see wireSource's onAuthStatusChanged).
            qCWarning(lcMainWindow) << "catalog.listPlaylists failed for" << client->sourceId() << ":" << e.what();
        }
    }
    sidebarModel_->setSource(client->sourceId(), client->sourceName(), playlists);
    // setSource() just recreated this source's header row from scratch,
    // dropping any warning/loading/error icon it had — reapply from the
    // cached state. Needed because this coroutine and the auth.start flow
    // kicked off alongside it in wireSource() race: an auth/prompt can
    // arrive and set the icon before this RPC round-trip finishes, in which
    // case this call would otherwise silently wipe it back off.
    updateSourceAuthIndicator(client->sourceId());
    sidebarModel_->setSourceLoading(client->sourceId(), client->sourceName(), false);
    sidebarModel_->setSourceFetchError(client->sourceId(), client->sourceName(), fetchTimedOut);
    if (fetchTimedOut) {
        toastNotifier_->showError(tr("%1: timed out loading playlists").arg(client->sourceName()));
    }

    // Startup restore of the last active playlist, once its source has
    // listed it — unless something else became active in the meantime.
    if (!activeContext_.isValid() && pendingRestore_.sourceId == client->sourceId()) {
        for (const Playlist& playlist : playlists) {
            if (playlist.id != pendingRestore_.playlistId)
                continue;
            setActiveContext(ActiveContext { client->sourceId(), playlist });
            loadActiveTracksAsync(activeContext_).detach();
            break;
        }
        pendingRestore_ = Config::Settings::ActivePlaylistRef();
    }
    // setSource() rebuilt this source's rows, dropping their selection.
    syncSidebarSelection();
    if (currentStatusPanelSourceId_ == client->sourceId())
        sourcePanel_->setPlaylists(playlists, /*loading=*/false);
}

void MainWindow::onSidebarActivated(const QModelIndex& index)
{
    const auto kind = static_cast<SidebarModel::Kind>(index.data(SidebarModel::KindRole).toInt());
    if (kind == SidebarModel::Kind::History) {
        if (activeContext_.isHistory)
            closeSheet();
        else
            openHistoryInSheet();
        return;
    }
    if (kind == SidebarModel::Kind::SourceHeader) {
        // Unconditional — every source gets a panel (name/description/
        // capabilities), not just ones with an auth problem. See
        // SourcePanel's class doc.
        showSourceStatusPanel(index.data(SidebarModel::SourceIdRole).toString());
        return;
    }
    if (kind != SidebarModel::Kind::Wave && kind != SidebarModel::Kind::Liked && kind != SidebarModel::Kind::Playlist)
        return;
    const QString sourceId = index.data(SidebarModel::SourceIdRole).toString();
    const Playlist playlist = index.data(SidebarModel::PlaylistDataRole).value<Playlist>();
    // The active playlist is what the main area already shows — clicking
    // it just gets the sheet out of the way.
    if (activeContext_.sameAs(ActiveContext { sourceId, playlist })) {
        closeSheet();
        return;
    }
    openInSheetAsync(sourceId, playlist).detach();
}

void MainWindow::onSidebarDoubleClicked(const QModelIndex& index)
{
    const auto kind = static_cast<SidebarModel::Kind>(index.data(SidebarModel::KindRole).toInt());
    if (kind == SidebarModel::Kind::History) {
        activate(historyContext(), { }, 0); // activate() fills History's queue itself
        return;
    }
    if (kind != SidebarModel::Kind::Wave && kind != SidebarModel::Kind::Liked && kind != SidebarModel::Kind::Playlist)
        return;
    const QString sourceId = index.data(SidebarModel::SourceIdRole).toString();
    const Playlist playlist = index.data(SidebarModel::PlaylistDataRole).value<Playlist>();
    activateAndPlayAsync(sourceId, playlist).detach();
}

void MainWindow::onSidebarContextMenuRequested(const QPoint& pos)
{
    const QModelIndex index = sidebarView_->indexAt(pos);
    if (!index.isValid())
        return;
    const auto kind = static_cast<SidebarModel::Kind>(index.data(SidebarModel::KindRole).toInt());
    const QString sourceId = index.data(SidebarModel::SourceIdRole).toString();

    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    if (kind == SidebarModel::Kind::SourceHeader) {
        menu->addAction(Theme::icon(QStringLiteral("refresh"), Theme::IconColor::Ink, 16),
            tr("Force Refresh Playlists"), this, [this, sourceId]() {
                Rpc::RpcClient* client = sourceManager_.client(sourceId);
                if (client != nullptr)
                    loadPlaylistsAsync(client).detach();
            });
    } else if (kind == SidebarModel::Kind::Wave || kind == SidebarModel::Kind::Liked
        || kind == SidebarModel::Kind::Playlist) {
        menu->addAction(
            Theme::icon(QStringLiteral("play_arrow"), Theme::IconColor::Ink, 16), tr("Play"), this, [this, index]() {
                const QString sourceId = index.data(SidebarModel::SourceIdRole).toString();
                const Playlist playlist = index.data(SidebarModel::PlaylistDataRole).value<Playlist>();
                activateAndPlayAsync(sourceId, playlist).detach();
            });
    } else {
        menu->deleteLater();
        return;
    }

    menu->popup(sidebarView_->viewport()->mapToGlobal(pos));
}

MainWindow::ActiveContext MainWindow::historyContext() const
{
    ActiveContext context;
    context.isHistory = true;
    context.playlist = Playlist { QStringLiteral("history"), tr("History"), std::nullopt, std::nullopt,
        static_cast<int>(playbackHistory_->entries().size()), QStringLiteral("playlist") };
    return context;
}

void MainWindow::setActiveContext(const ActiveContext& context)
{
    const bool changed = !activeContext_.sameAs(context);
    activeContext_ = context;
    if (changed)
        activeTracks_.clear();
    sidebarModel_->setActivePlaylist(context.isHistory ? QString() : context.sourceId,
        context.isHistory ? QStringLiteral("history") : context.playlist.id);
    if (context.persistent) {
        settings_.setLastActivePlaylist({ context.isHistory ? QString() : context.sourceId, context.playlist.id,
            context.isHistory ? QStringLiteral("history") : context.playlist.kind });
    }
    refreshHero();
    refreshMainList();
    syncSidebarSelection();
}

void MainWindow::activate(const ActiveContext& context, const QVector<Playback::QueueEntry>& entries, int startIndex)
{
    if (context.isRadio()) {
        startRadioAsync(context.sourceId, context.playlist.id, context).detach();
        closeSheet();
        return;
    }
    QVector<Playback::QueueEntry> queue = entries;
    if (queue.isEmpty() && context.isHistory) {
        for (const History::HistoryEntry& e : playbackHistory_->entries())
            queue.append(Playback::QueueEntry { e.sourceId, e.track });
    }
    if (queue.isEmpty())
        return;
    setActiveContext(context);
    playback_.loadQueue(queue, qBound(0, startIndex, int(queue.size()) - 1));
    closeSheet();
}

Rpc::Task<QVector<Playback::QueueEntry>> MainWindow::fetchTracksAsync(QString sourceId, Playlist playlist)
{
    QVector<Playback::QueueEntry> entries;
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return entries;
    QList<Track> tracks;
    if (playlist.kind == QStringLiteral("liked")) {
        ListLikedParams params { std::nullopt };
        tracks = (co_await Rpc::catalogListLiked(*client, params)).tracks;
    } else {
        ListTracksParams params { playlist.id, std::nullopt };
        tracks = (co_await Rpc::catalogListTracks(*client, params)).tracks;
    }
    trackStates_->observe(sourceId, tracks);
    entries.reserve(tracks.size());
    for (const Track& track : tracks)
        entries.append(Playback::QueueEntry { sourceId, track });
    co_return entries;
}

Rpc::Task<void> MainWindow::activateAndPlayAsync(QString sourceId, Playlist playlist)
{
    const ActiveContext context { sourceId, playlist };
    if (context.isRadio()) {
        activate(context, { }, 0);
        co_return;
    }
    try {
        const QVector<Playback::QueueEntry> entries = co_await fetchTracksAsync(sourceId, playlist);
        activate(context, entries, 0);
    } catch (const std::exception& e) {
        qCWarning(lcMainWindow) << "loading tracks failed for" << sourceId << ":" << e.what();
        toastNotifier_->showError(QString::fromStdString(e.what()));
    }
}

Rpc::Task<void> MainWindow::loadActiveTracksAsync(ActiveContext context)
{
    if (context.isRadio())
        co_return;
    QVector<Playback::QueueEntry> entries;
    if (context.isHistory) {
        for (const History::HistoryEntry& e : playbackHistory_->entries())
            entries.append(Playback::QueueEntry { e.sourceId, e.track });
    } else {
        trackListBusyIndicator_->show();
        try {
            entries = co_await fetchTracksAsync(context.sourceId, context.playlist);
        } catch (const std::exception& e) {
            qCWarning(lcMainWindow) << "loading tracks failed for" << context.sourceId << ":" << e.what();
        }
        trackListBusyIndicator_->hide();
    }
    // Only if it's still the active playlist and nothing got queued meanwhile.
    if (!activeContext_.sameAs(context))
        co_return;
    activeTracks_ = entries;
    refreshMainList();
}

void MainWindow::refreshMainList()
{
    const bool hasActive = activeContext_.isValid() || playback_.hasQueue();
    emptyStatePlaceholder_->setVisible(!hasActive);
    contentSplitter_->setVisible(hasActive);

    const QVector<Playback::QueueEntry>& entries = playback_.hasQueue() ? playback_.queue() : activeTracks_;
    QList<TrackListModel::MixedSourceEntry> rows;
    rows.reserve(entries.size());
    for (const Playback::QueueEntry& entry : entries)
        rows.append({ entry.sourceId, entry.track, QDateTime() });
    // A radio's tracksAdded refreshes this while the user may be scrolled
    // down the list — keep their place across the model reset.
    const int scroll = trackListView_->verticalScrollBar()->value();
    trackListModel_->setMixedSourceTracks(rows);
    trackListView_->verticalScrollBar()->setValue(scroll);

    // A radio station that hasn't started yet has nothing to list — the
    // hero (with its Play button) takes the whole width then.
    const bool showList = !(rows.isEmpty() && activeContext_.isRadio());
    if (showList != trackListPane_->isVisibleTo(contentSplitter_))
        setTrackListVisible(showList);
}

void MainWindow::refreshHero()
{
    if (playback_.hasCurrentTrack())
        return; // trackChanged keeps the hero on the playing track
    if (!activeContext_.isValid()) {
        heroPanel_->clearNowPlaying();
        return;
    }
    heroPanel_->setPlaylist(activeContext_.playlist);
    heroPanel_->setPlayButtonVisible(true);
}

void MainWindow::syncSidebarSelection()
{
    QModelIndex target;
    if (sheet_->isPresented()) {
        if (sheetContext_.isValid())
            target = sidebarModel_->indexForPlaylist(sheetContext_.isHistory ? QString() : sheetContext_.sourceId,
                sheetContext_.isHistory ? QStringLiteral("history") : sheetContext_.playlist.id);
        else if (!currentStatusPanelSourceId_.isEmpty())
            // A source page: its header row. Not left as is — setSource()
            // recreates that row on every reload, and the view would
            // otherwise leave the selection on whatever row slid into its place.
            target = sidebarModel_->indexForSource(currentStatusPanelSourceId_);
    } else if (activeContext_.isValid()) {
        target = sidebarModel_->indexForPlaylist(activeContext_.isHistory ? QString() : activeContext_.sourceId,
            activeContext_.isHistory ? QStringLiteral("history") : activeContext_.playlist.id);
    }
    if (target.isValid())
        sidebarView_->setCurrentIndex(target);
    else
        sidebarView_->clearSelection();
}

void MainWindow::playActive()
{
    if (!activeContext_.isValid())
        return;
    if (activeContext_.isRadio()) {
        startRadioAsync(activeContext_.sourceId, activeContext_.playlist.id, activeContext_).detach();
    } else if (playback_.hasQueue()) {
        playback_.playAt(0);
    } else {
        activate(activeContext_, activeTracks_, 0);
    }
}

Rpc::Task<void> MainWindow::openInSheetAsync(QString sourceId, Playlist playlist)
{
    currentStatusPanelSourceId_.clear();
    sheetContext_ = ActiveContext { sourceId, playlist };
    const QString coverUrl = playlist.coverUrl.value_or(QString());
    if (sheetContext_.isRadio()) {
        sheet_->trackModel()->clear();
        sheet_->showRadio(playlist.title, playlist.description.value_or(QString()), coverUrl, playlist.title);
        sheet_->present();
        co_return;
    }
    sheet_->trackModel()->clear();
    sheet_->showTracks(playlist.title, trackCountText(playlist.trackCount), coverUrl, playlist.title,
        /*canPlayAll=*/true);
    sheet_->present();

    sheet_->setBusy(true);
    try {
        const QVector<Playback::QueueEntry> entries = co_await fetchTracksAsync(sourceId, playlist);
        // The user may have opened something else while this was loading.
        if (!sheetContext_.sameAs(ActiveContext { sourceId, playlist }))
            co_return;
        QList<TrackListModel::MixedSourceEntry> rows;
        rows.reserve(entries.size());
        for (const Playback::QueueEntry& entry : entries)
            rows.append({ entry.sourceId, entry.track, QDateTime() });
        sheet_->trackModel()->setMixedSourceTracks(rows);
        sheet_->setSubtitle(trackCountText(int(rows.size())));
    } catch (const std::exception& e) {
        qCWarning(lcMainWindow) << "loading tracks failed for" << sourceId << ":" << e.what();
        toastNotifier_->showError(QString::fromStdString(e.what()));
    }
    if (sheetContext_.sameAs(ActiveContext { sourceId, playlist }))
        sheet_->setBusy(false);
}

void MainWindow::openHistoryInSheet()
{
    currentStatusPanelSourceId_.clear();
    sheetContext_ = historyContext();
    sheet_->showTracks(tr("History"), QString(), QString(), QStringLiteral("history"), /*canPlayAll=*/true);
    sheet_->setBusy(false);
    fillHistorySheet();
    sheet_->present();
}

void MainWindow::fillHistorySheet()
{
    QList<TrackListModel::MixedSourceEntry> entries;
    entries.reserve(playbackHistory_->entries().size());
    for (const History::HistoryEntry& e : playbackHistory_->entries())
        entries.append({ e.sourceId, e.track, e.playedAt });
    sheet_->trackModel()->setMixedSourceTracks(entries);
    sheet_->setSubtitle(trackCountText(int(entries.size())));
}

void MainWindow::closeSheet() { sheet_->dismiss(); }

void MainWindow::activateFromSheet(int row)
{
    TrackListModel* model = sheet_->trackModel();
    if (row < 0 || row >= model->rowCount())
        return;
    QVector<Playback::QueueEntry> entries;
    entries.reserve(model->rowCount());
    for (int i = 0; i < model->rowCount(); ++i)
        entries.append(Playback::QueueEntry { model->sourceIdAt(i), model->trackAt(i) });
    activate(sheetContext_, entries, row);
}

void MainWindow::playAllFromSheet()
{
    if (sheetContext_.isRadio()) {
        activate(sheetContext_, { }, 0);
        return;
    }
    activateFromSheet(0);
}

Rpc::Task<void> MainWindow::startRadioAsync(QString sourceId, QString seed, ActiveContext context)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    heroPanel_->setPlayBusy(true);
    try {
        StartRadioParams params { seed };
        StartRadioResult result = co_await Rpc::catalogStartRadio(*client, params);
        trackStates_->observe(sourceId, result.initialTracks);
        // Before startRadio(), so the queue it emits lands in the main
        // list under the right playlist.
        setActiveContext(context);
        playback_.startRadio(sourceId, result.stationId, result.initialTracks);
    } catch (const std::exception& e) {
        qCWarning(lcMainWindow) << "starting radio failed for" << sourceId << ":" << e.what();
        toastNotifier_->showError(QString::fromStdString(e.what()));
    }
    heroPanel_->setPlayBusy(false);
}

void MainWindow::onTrackDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid())
        return;
    // The main list mirrors the queue once there is one (see
    // refreshMainList()), so its rows are queue indices.
    if (playback_.hasQueue())
        playback_.playAt(index.row());
    else
        activate(activeContext_, activeTracks_, index.row());
}

void MainWindow::onTrackContextMenuRequested(const QPoint& pos)
{
    const QModelIndex index = trackListView_->indexAt(pos);
    if (!index.isValid())
        return;
    const int row = index.row();
    showTrackMenu(trackListModel_->sourceIdAt(row), trackListModel_->trackAt(row),
        trackListView_->viewport()->mapToGlobal(pos), [this, index]() { onTrackDoubleClicked(index); });
}

void MainWindow::showTrackMenu(
    const QString& sourceId, const Track& track, const QPoint& globalPos, std::function<void()> play)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    const QJsonObject capabilities = client != nullptr ? client->capabilities() : QJsonObject();
    const QJsonObject feedback = capabilities.value(QStringLiteral("feedback")).toObject();
    const bool likeSupported = feedback.value(QStringLiteral("like")).toBool();
    const bool dislikeSupported = feedback.value(QStringLiteral("dislike")).toBool();
    const bool radioSupported
        = capabilities.value(QStringLiteral("browse")).toObject().value(QStringLiteral("radio")).toBool();
    const bool downloadSupported = capabilities.value(QStringLiteral("download")).toBool();
    const QString webUrl = track.webUrl.value_or(QString());

    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // Icon + text, Theme::IconColor::Ink at 16px — same convention
    // Integration::TrayIcon's menu already uses for its own QAction icons.
    menu->addAction(
        Theme::icon(QStringLiteral("play_arrow"), Theme::IconColor::Ink, 16), tr("Play"), this, std::move(play));
    menu->addAction(Theme::icon(QStringLiteral("playlist_play"), Theme::IconColor::Ink, 16), tr("Play Next"), this,
        [this, sourceId, track]() { playback_.enqueueNext(sourceId, track); });
    menu->addAction(Theme::icon(QStringLiteral("playlist_add"), Theme::IconColor::Ink, 16), tr("Add to Queue"), this,
        [this, sourceId, track]() { playback_.enqueueAtEnd(sourceId, track); });

    if (likeSupported || dislikeSupported) {
        menu->addSeparator();
        if (likeSupported) {
            // State shown by the icon (filled accent heart), not a check box:
            // Fusion frames a checked item's icon, which reads as a stray border.
            const bool liked = trackStates_->state(sourceId, track.id).liked.value_or(false);
            QAction* likeAction
                = menu->addAction(liked ? Theme::icon(QStringLiteral("favorite"), Theme::IconColor::Accent, 16)
                                        : Theme::icon(QStringLiteral("favorite_border"), Theme::IconColor::Ink, 16),
                    liked ? tr("Unlike") : tr("Like"));
            connect(likeAction, &QAction::triggered, this, [this, sourceId, id = track.id, liked]() {
                likeToggledAsync(sourceId, id, !liked, /*announceSuccess=*/true).detach();
            });
        }
        if (dislikeSupported) {
            const bool disliked = trackStates_->state(sourceId, track.id).disliked.value_or(false);
            QAction* dislikeAction
                = menu->addAction(Theme::icon(QStringLiteral("heart_broken"),
                                      disliked ? Theme::IconColor::Accent : Theme::IconColor::Ink, 16),
                    disliked ? tr("Remove Dislike") : tr("Dislike"));
            connect(dislikeAction, &QAction::triggered, this, [this, sourceId, id = track.id, disliked]() {
                dislikeToggledAsync(sourceId, id, !disliked, /*announceSuccess=*/true).detach();
            });
        }
    }

    if (radioSupported) {
        menu->addSeparator();
        // The bare track id — each backend that declares browse.radio
        // decides for itself how to turn a track id into whatever
        // station-addressing scheme it needs (e.g. Yandex's rotor API
        // wants a "track:<id>"-style address; YouTube's get_watch_playlist
        // takes a plain videoId directly). A front-imposed prefix here
        // broke YouTube radio (it received "track:<id>" as a literal,
        // invalid videoId) — seed is source-defined per docs/protocol.md
        // §7.1, so the front must not format it.
        menu->addAction(Theme::icon(QStringLiteral("radio"), Theme::IconColor::Ink, 16),
            tr("Start Radio from This Track"), this, [this, sourceId, id = track.id, title = track.title]() {
                // An ad-hoc station — active while it plays, but not one to
                // restore at startup.
                ActiveContext context { sourceId,
                    Playlist { id, tr("Radio: %1").arg(title), std::nullopt, std::nullopt, 0,
                        QStringLiteral("radioStation") } };
                context.persistent = false;
                startRadioAsync(sourceId, id, context).detach();
            });
    }

    if (!webUrl.isEmpty() || downloadSupported) {
        menu->addSeparator();
        if (!webUrl.isEmpty()) {
            menu->addAction(Theme::icon(QStringLiteral("open_in_new"), Theme::IconColor::Ink, 16),
                tr("Open Track Page"), this, [webUrl]() { QDesktopServices::openUrl(QUrl(webUrl)); });
        }
        if (downloadSupported) {
            menu->addAction(Theme::icon(QStringLiteral("file_download"), Theme::IconColor::Ink, 16),
                tr("Save to Downloads"), this,
                [this, sourceId, track]() { downloadTrackAsync(sourceId, track).detach(); });
        }
    }

    menu->popup(globalPos);
}

Rpc::Task<void> MainWindow::submitAuthAsync(QString sourceId, QJsonObject fields)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    sourcePanel_->setAuthActionBusy(true);
    try {
        SubmitParams params;
        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
            params.fields.insert(it.key(), it.value().toString());
        }
        co_await Rpc::authSubmit(*client, params);
    } catch (const std::exception& e) {
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcMainWindow) << "auth.submit failed for" << sourceId << ":" << message;
        toastNotifier_->showError(tr("%1: %2").arg(client->sourceName(), message));
        sourcePanel_->showError(message);
    }
    sourcePanel_->setAuthActionBusy(false);
}

Rpc::Task<void> MainWindow::likeToggledAsync(QString sourceId, QString trackId, bool liked, bool announceSuccess)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr || !client->available())
        co_return;
    const QJsonObject feedback = client->capabilities().value(QStringLiteral("feedback")).toObject();
    const bool likeSupported = feedback.value(QStringLiteral("like")).toBool();
    const bool dislikeSupported = feedback.value(QStringLiteral("dislike")).toBool();
    // The track (or the whole queue) may have changed by the time the
    // co_await below resumes — only touch nowPlayingBar_ if it's still
    // showing the track this click was for; the caches below are patched
    // unconditionally, since they matter regardless of what's on screen.
    auto stillCurrent = [this, sourceId, trackId]() {
        return playback_.hasCurrentTrack() && playback_.currentSourceId() == sourceId
            && playback_.currentTrack().id == trackId;
    };

    if (stillCurrent())
        nowPlayingBar_->setLikeBusy(true);
    try {
        if (liked)
            co_await Rpc::feedbackLike(*client, LikeParams { trackId });
        else
            co_await Rpc::feedbackUnlike(*client, UnlikeParams { trackId });
        trackStates_->setLiked(sourceId, trackId, liked);
        playbackHistory_->markTrackLiked(sourceId, trackId, liked); // keeps the saved snapshot fresh
        if (stillCurrent()) {
            nowPlayingBar_->setLikeState(likeSupported, liked);
            // Both backends cross-clear the opposite rating server-side on
            // a successful like (see docs/protocol.md §7.4) — mirror that
            // locally so the UI never shows both lit up at once.
            if (liked)
                nowPlayingBar_->setDislikeState(dislikeSupported, false);
        }
        if (announceSuccess)
            toastNotifier_->showInfo(liked ? tr("Added to Liked") : tr("Removed from Liked"));
    } catch (const std::exception& e) {
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcMainWindow) << "feedback.like/unlike failed for" << trackId << ":" << message;
        toastNotifier_->showError(tr("%1: %2").arg(client->sourceName(), message));
        if (stillCurrent())
            nowPlayingBar_->setLikeState(likeSupported, !liked);
    }
}

Rpc::Task<void> MainWindow::dislikeToggledAsync(QString sourceId, QString trackId, bool disliked, bool announceSuccess)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr || !client->available())
        co_return;
    const QJsonObject feedback = client->capabilities().value(QStringLiteral("feedback")).toObject();
    const bool likeSupported = feedback.value(QStringLiteral("like")).toBool();
    const bool dislikeSupported = feedback.value(QStringLiteral("dislike")).toBool();
    auto stillCurrent = [this, sourceId, trackId]() {
        return playback_.hasCurrentTrack() && playback_.currentSourceId() == sourceId
            && playback_.currentTrack().id == trackId;
    };

    if (stillCurrent())
        nowPlayingBar_->setDislikeBusy(true);
    try {
        if (disliked)
            co_await Rpc::feedbackDislike(*client, DislikeParams { trackId });
        else
            co_await Rpc::feedbackUndislike(*client, UndislikeParams { trackId });
        // Unconditional, unlike the NowPlayingBar update below (see
        // likeToggledAsync's stillCurrent() doc comment). setDisliked()
        // also cross-clears Like — docs/protocol.md §7.4.
        trackStates_->setDisliked(sourceId, trackId, disliked);
        if (disliked)
            playbackHistory_->markTrackLiked(sourceId, trackId, false);
        if (stillCurrent()) {
            nowPlayingBar_->setDislikeState(dislikeSupported, disliked);
            if (disliked) {
                nowPlayingBar_->setLikeState(likeSupported, false);
                // No point listening to a track just disliked — move on,
                // like the services' own players do. next() also sends the
                // skip feedback a radio uses to adapt its upcoming tracks.
                playback_.next();
            }
        }
        if (announceSuccess)
            toastNotifier_->showInfo(disliked ? tr("Disliked") : tr("Removed dislike"));
    } catch (const std::exception& e) {
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcMainWindow) << "feedback.dislike/undislike failed for" << trackId << ":" << message;
        toastNotifier_->showError(tr("%1: %2").arg(client->sourceName(), message));
        if (stillCurrent())
            nowPlayingBar_->setDislikeState(dislikeSupported, !disliked);
    }
}

Rpc::Task<void> MainWindow::downloadTrackAsync(QString sourceId, Track track)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr || !client->available())
        co_return;
    toastNotifier_->showInfo(tr("Downloading \"%1\"…").arg(track.title));
    try {
        DownloadTrackParams params { track.id, settings_.downloadDirectory() };
        DownloadTrackResult result = co_await Rpc::catalogDownloadTrack(*client, params);
        Q_UNUSED(result);
        toastNotifier_->showInfo(tr("Saved \"%1\"").arg(track.title));
    } catch (const std::exception& e) {
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcMainWindow) << "catalog.downloadTrack failed for" << track.id << ":" << message;
        toastNotifier_->showError(tr("%1: %2").arg(client->sourceName(), message));
    }
}

Rpc::Task<void> MainWindow::downloadCurrentTrackAsync()
{
    if (!playback_.hasCurrentTrack())
        co_return;
    const QString sourceId = playback_.currentSourceId();
    const QString trackId = playback_.currentTrack().id;
    const Track track = playback_.currentTrack();
    auto stillCurrent = [this, sourceId, trackId]() {
        return playback_.hasCurrentTrack() && playback_.currentSourceId() == sourceId
            && playback_.currentTrack().id == trackId;
    };

    if (stillCurrent())
        nowPlayingBar_->setDownloadBusy(true);
    co_await downloadTrackAsync(sourceId, track); // toasts + the RPC call itself
    if (stillCurrent())
        nowPlayingBar_->setDownloadBusy(false);
}

void MainWindow::refreshNowPlayingFeedback()
{
    if (!playback_.hasCurrentTrack())
        return;
    const QString sourceId = playback_.currentSourceId();
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    const QJsonObject feedback
        = client != nullptr ? client->capabilities().value(QStringLiteral("feedback")).toObject() : QJsonObject();
    const Library::TrackState state = trackStates_->state(sourceId, playback_.currentTrack().id);
    nowPlayingBar_->setLikeState(feedback.value(QStringLiteral("like")).toBool(), state.liked.value_or(false));
    nowPlayingBar_->setDislikeState(feedback.value(QStringLiteral("dislike")).toBool(), state.disliked.value_or(false));
}

void MainWindow::showAboutDialog() { Ui::AboutDialog(this).exec(); }

void MainWindow::quitForReal()
{
    reallyQuitting_ = true;
    close();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!reallyQuitting_ && settings_.closeMinimizesToTray()) {
        event->ignore();
        hide();
        return;
    }
    emit aboutToReallyQuit();
    event->accept();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize) {
        repositionTrackListBusyIndicator();
        if (watched == trackListContainer_)
            sheet_->setGeometry(trackListContainer_->rect());
    }
    if (watched == sidebarView_->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            updateSidebarHover(sidebarView_->indexAt(static_cast<QMouseEvent*>(event)->pos()));
        } else if (event->type() == QEvent::Leave) {
            updateSidebarHover(QModelIndex());
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateSidebarHover(const QModelIndex& index)
{
    const QModelIndex previous = sidebarDelegate_->hoveredIndex();
    if (previous == index)
        return;
    sidebarDelegate_->setHoveredIndex(index);
    // Full row width, not just visualRect() — NavItemDelegate now paints
    // hover/selected backgrounds across the whole row (indentation/branch
    // area included), so invalidating only the narrow item-column rect
    // would leave that left strip stale.
    const auto fullRowRect = [this](const QModelIndex& idx) {
        const QRect item = sidebarView_->visualRect(idx);
        return QRect(0, item.top(), sidebarView_->viewport()->width(), item.height());
    };
    if (previous.isValid())
        sidebarView_->viewport()->update(fullRowRect(previous));
    if (index.isValid())
        sidebarView_->viewport()->update(fullRowRect(index));
}

void MainWindow::repositionTrackListBusyIndicator()
{
    trackListBusyIndicator_->setGeometry(trackListView_->x(), trackListView_->y(), trackListView_->width(), 4);
}

void MainWindow::setTrackListVisible(bool visible)
{
    trackListPane_->setVisible(visible);
    if (visible) {
        // Restore the persisted/current hero-vs-list ratio. QSplitter
        // interprets setSizes() by ratio, not absolute sum, so re-asserting
        // settings_.heroPanelWidth() here is correct whether this is the
        // very first show or a restore right after a My Wave collapse.
        contentSplitter_->setSizes({ settings_.heroPanelWidth(), 290 });
    } else {
        // Collapse: hand the whole splitter width to heroPanel_ — the
        // radioStation (My Wave) case, where there's no track list at all.
        contentSplitter_->setSizes({ contentSplitter_->width(), 0 });
    }
    // Unlike the old QVBoxLayout::setStretchFactor()-based version of this
    // method (which needed an explicit activate() to force a pending
    // LayoutRequest through synchronously — see the equivalent comment in
    // git history), QSplitter::setSizes() resizes its children directly
    // and immediately, not via a deferred layout pass — every caller here
    // still calls this before HeroPanel::setPlaylist()/setNowPlaying(),
    // which read heroPanel_'s size() to decide how large a generated
    // cover/overlay to render (see GeneratedCoverArt.h), and that already
    // sees the post-setSizes() size with no extra step needed.
}

} // namespace Ui
