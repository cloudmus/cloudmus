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
#include "RpcMethods.h"
#include "SettingsDialog.h"
#include "SidebarModel.h"
#include "SmoothScroller.h"
#include "SourcePanel.h"
#include "Spacing.h"
#include "ThemedSplitter.h"
#include "ToastNotifier.h"
#include "TrackListModel.h"
#include "TrackRowDelegate.h"

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
            heroPanel_->setPlaylist(currentPlaylist_);
            trackRowDelegate_->setCurrentlyPlaying(QString(), QString());
            trackListView_->viewport()->update();
        }
    });
    connect(&playback_, &Playback::PlaybackController::queueAvailabilityChanged, this,
        [this](bool available) { nowPlayingBar_->setQueueAvailable(available); });

    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString& sourceId) { playbackHistory_->record(sourceId, track); });
    connect(&playback_, &Playback::PlaybackController::trackChanged, this, [this](const Track& track, const QString&) {
        nowPlayingBar_->setTrackWebUrl(track.webUrl.value_or(QString()));
    });
    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString& sourceId) {
            const Rpc::RpcClient* client = sourceManager_.client(sourceId);
            const QJsonObject capabilities = client != nullptr ? client->capabilities() : QJsonObject();
            const QJsonObject feedback = capabilities.value(QStringLiteral("feedback")).toObject();
            nowPlayingBar_->setLikeState(feedback.value(QStringLiteral("like")).toBool(), track.liked.value_or(false));
            nowPlayingBar_->setDislikeState(feedback.value(QStringLiteral("dislike")).toBool(), /*disliked=*/false);
            nowPlayingBar_->setDownloadState(capabilities.value(QStringLiteral("download")).toBool());
        });
    // HeroPanel shows what's playing instead of the browsed playlist's
    // promo card whenever playback_.hasCurrentTrack() — see
    // showPlaylistAsync()/showHistory()'s guard, and the
    // currentTrackAvailabilityChanged handler above for how Stop reverts it.
    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString&) { heroPanel_->setNowPlaying(track); });
    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString& sourceId) {
            trackRowDelegate_->setCurrentlyPlaying(sourceId, track.id);
            trackListView_->viewport()->update();
        });
    connect(playbackHistory_, &History::PlaybackHistory::changed, this, [this]() {
        if (showingHistory_)
            showHistory(); // refresh in place — a track just started playing
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

    heroPanel_ = new HeroPanel(coverArtCache_, this);
    // Always the tall full-height splitter pane below, regardless of
    // whether trackListPane_ is currently visible — unlike the old
    // PlaylistHeader, this is never toggled again after construction (see
    // setTrackListVisible()).
    heroPanel_->setFillMode(true);
    connect(heroPanel_, &HeroPanel::playClicked, this, &MainWindow::playCurrentPlaylist);

    trackListModel_ = new TrackListModel(this);
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
    OverlayScrollBar::attach(trackListView_);
    connect(trackListView_, &QListView::doubleClicked, this, &MainWindow::onTrackDoubleClicked);
    connect(trackRowDelegate_, &TrackRowDelegate::playRequested, this, &MainWindow::onTrackDoubleClicked);
    trackListView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(trackListView_, &QListView::customContextMenuRequested, this, &MainWindow::onTrackContextMenuRequested);

    auto* trackListContainer = new QWidget(this);
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

    contentSplitter_ = new ThemedSplitter(Qt::Horizontal, trackListContainer);
    contentSplitter_->addWidget(heroPanel_);
    contentSplitter_->addWidget(trackListPane_);
    contentSplitter_->setSizes({ settings_.heroPanelWidth(), 290 });
    connect(contentSplitter_, &QSplitter::splitterMoved, this,
        [this]() { settings_.setHeroPanelWidth(contentSplitter_->sizes().first()); });
    trackListContainerLayout->addWidget(contentSplitter_, 1);

    sourcePanel_ = new SourcePanel(coverArtCache_, trackListContainer);
    connect(sourcePanel_, &SourcePanel::submitRequested, this,
        [this](const QString& sourceId, const QJsonObject& fields) { submitAuthAsync(sourceId, fields).detach(); });
    connect(sourcePanel_, &SourcePanel::retryRequested, this,
        [this](const QString& sourceId) { retryAuthAsync(sourceId).detach(); });
    trackListContainerLayout->addWidget(sourcePanel_, 1);

    // Shown by default (contentSplitter_ hidden below) until the first
    // playlist/History/source selection swaps it out — see
    // showPlaylistAsync()/showHistory()/showSourceStatusPanel().
    emptyStatePlaceholder_ = new EmptyStatePlaceholder(trackListContainer);
    trackListContainerLayout->addWidget(emptyStatePlaceholder_, 1);
    contentSplitter_->hide();

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

    auto* splitter = new ThemedSplitter(this);
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
}

MainWindow::~MainWindow() { settings_.setWindowGeometry(saveGeometry()); }

void MainWindow::wireSource(Rpc::RpcClient* client)
{
    client->notifications.onTrackStreamReady
        = [this, client](const StreamReadyParams& p) { playback_.handleStreamReady(client->sourceId(), p); };
    client->notifications.onRadioTracksAdded
        = [this, client](const TracksAddedParams& p) { playback_.handleTracksAdded(client->sourceId(), p); };
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
    showingHistory_ = false;
    currentPlaylistSourceId_.clear();
    emptyStatePlaceholder_->hide();
    contentSplitter_->hide();
    trackListBusyIndicator_->hide();
    currentStatusPanelSourceId_ = sourceId;

    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    const QString sourceName = client != nullptr ? client->sourceName() : sourceId;
    const QString description = client != nullptr ? client->sourceDescription() : QString();
    const QJsonObject capabilities = client != nullptr ? client->capabilities() : QJsonObject();
    sourcePanel_->setSource(sourceId, sourceName, description, capabilities);

    refreshAuthSection(sourceId, sourceAuthStates_.value(sourceId));
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
    toastNotifier_->showError(tr("%1 is unavailable").arg(name));
}

Rpc::Task<void> MainWindow::loadPlaylistsAsync(Rpc::RpcClient* client)
{
    const QJsonObject browse = client->capabilities().value(QStringLiteral("browse")).toObject();
    const bool shouldFetch = browse.value(QStringLiteral("playlists")).toBool()
        || browse.value(QStringLiteral("likedTracks")).toBool() || browse.value(QStringLiteral("radio")).toBool();
    QList<Playlist> playlists;
    if (shouldFetch) {
        try {
            ListPlaylistsResult result = co_await Rpc::catalogListPlaylists(*client);
            playlists = result.playlists;
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
    // dropping any warning icon it had — reapply from the cached state.
    // Needed because this coroutine and the auth.start flow kicked off
    // alongside it in wireSource() race: an auth/prompt can arrive and set
    // the icon before this RPC round-trip finishes, in which case this call
    // would otherwise silently wipe it back off.
    updateSourceAuthIndicator(client->sourceId());
}

void MainWindow::onSidebarActivated(const QModelIndex& index)
{
    const auto kind = static_cast<SidebarModel::Kind>(index.data(SidebarModel::KindRole).toInt());
    if (kind == SidebarModel::Kind::History) {
        showHistory();
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
    showPlaylistAsync(sourceId, playlist).detach();
}

void MainWindow::onSidebarDoubleClicked(const QModelIndex& index)
{
    const auto kind = static_cast<SidebarModel::Kind>(index.data(SidebarModel::KindRole).toInt());
    // History has no single queue to play as a whole (mixed sourceIds — see
    // TrackListModel::isMixedSource()); double-clicking it just opens it,
    // same as a single click, same as onSidebarActivated above.
    if (kind != SidebarModel::Kind::Wave && kind != SidebarModel::Kind::Liked && kind != SidebarModel::Kind::Playlist)
        return;
    const QString sourceId = index.data(SidebarModel::SourceIdRole).toString();
    const Playlist playlist = index.data(SidebarModel::PlaylistDataRole).value<Playlist>();
    openAndPlayPlaylistAsync(sourceId, playlist).detach();
}

void MainWindow::playCurrentPlaylist()
{
    if (currentPlaylistSourceId_.isEmpty())
        return;
    if (currentPlaylist_.kind == QStringLiteral("radioStation")) {
        startRadioAsync(currentPlaylistSourceId_, currentPlaylist_.id).detach();
    } else if (!trackListModel_->allTracks().isEmpty()) {
        playback_.loadQueue(currentPlaylistSourceId_, trackListModel_->allTracks(), 0);
    }
}

void MainWindow::showHistory()
{
    currentStatusPanelSourceId_.clear();
    sourcePanel_->hide();
    emptyStatePlaceholder_->hide();
    contentSplitter_->show();
    showingHistory_ = true;
    currentPlaylistSourceId_.clear(); // no single source — the header's Play-all button is hidden below anyway
    currentPlaylist_ = Playlist { QStringLiteral("history"), tr("History"), std::nullopt, std::nullopt,
        static_cast<int>(playbackHistory_->entries().size()), QStringLiteral("playlist") };
    // Before setPlaylist(), not after — see setTrackListVisible()'s comment.
    setTrackListVisible(true);
    // While something is playing, heroPanel_ stays showing that track
    // instead of switching back to a promo card for History — see
    // showPlaylistAsync()'s identical guard; Stop reverts this (see the
    // currentTrackAvailabilityChanged handler in the constructor).
    if (!playback_.hasCurrentTrack())
        heroPanel_->setPlaylist(currentPlaylist_);
    heroPanel_->setPlayButtonVisible(false);

    QList<TrackListModel::MixedSourceEntry> entries;
    entries.reserve(playbackHistory_->entries().size());
    for (const History::HistoryEntry& e : playbackHistory_->entries())
        entries.append({ e.sourceId, e.track, e.playedAt });
    trackListModel_->setMixedSourceTracks(entries);

    // Deferred to the next event-loop iteration, not called synchronously
    // here — heroPanel_->setPlaylist() above may have just posted a
    // LayoutRequest (its heightForWidth() may have changed — see its own
    // updateGeometry() call), which Qt only processes asynchronously.
    // Reading trackListView_->y() before that pass runs picks up
    // whatever position was left over from the *previous* playlist's
    // banner height, not the new one — this was the actual cause of the
    // busy indicator appearing to jump around at an arbitrary vertical
    // position after switching lists.
    QTimer::singleShot(0, this, [this]() { repositionTrackListBusyIndicator(); });
    trackListBusyIndicator_->hide();
}

Rpc::Task<void> MainWindow::showPlaylistAsync(QString sourceId, Playlist playlist)
{
    currentStatusPanelSourceId_.clear();
    sourcePanel_->hide();
    emptyStatePlaceholder_->hide();
    contentSplitter_->show();
    showingHistory_ = false;
    heroPanel_->setPlayButtonVisible(true);
    currentPlaylistSourceId_ = sourceId;
    currentPlaylist_ = playlist;

    // Before setPlaylist(), not after — see setTrackListVisible()'s comment:
    // it decides how large a generated cover/overlay to render from
    // heroPanel_'s *current* size(), which needs to already reflect this
    // splitter-size change.
    const bool isRadioStation = playlist.kind == QStringLiteral("radioStation");
    setTrackListVisible(!isRadioStation);
    // While something is playing, heroPanel_ stays showing the globally
    // playing track regardless of which playlist is browsed here — it
    // never reverts to this playlist's promo card until Stop is pressed
    // (see the currentTrackAvailabilityChanged handler in the constructor).
    if (!playback_.hasCurrentTrack())
        heroPanel_->setPlaylist(playlist);

    if (isRadioStation) {
        // Continuous, not a fixed list — see docs/protocol.md's
        // Playlist.kind note. Only the header + Play button show; no RPC
        // call here, that's what makes this not auto-play (the Play button
        // handler wired in the constructor calls startRadioAsync()).
        trackListModel_->clear();
        co_return;
    }

    // Deferred — see showHistory()'s identical call for why.
    QTimer::singleShot(0, this, [this]() { repositionTrackListBusyIndicator(); });
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;

    trackListBusyIndicator_->show();
    try {
        if (playlist.kind == QStringLiteral("liked")) {
            ListLikedParams params { std::nullopt };
            ListLikedResult result = co_await Rpc::catalogListLiked(*client, params);
            trackListModel_->setTracks(sourceId, result.tracks);
        } else {
            ListTracksParams params { playlist.id, std::nullopt };
            ListTracksResult result = co_await Rpc::catalogListTracks(*client, params);
            trackListModel_->setTracks(sourceId, result.tracks);
        }
    } catch (const std::exception& e) {
        // std::exception, not Rpc::RpcCallException — also catches
        // Rpc::ProtocolParseError, see loadPlaylistsAsync's comment for why
        // that distinction matters.
        qCWarning(lcMainWindow) << "loading tracks failed for" << sourceId << ":" << e.what();
        toastNotifier_->showError(QString::fromStdString(e.what()));
    }
    trackListBusyIndicator_->hide();
}

Rpc::Task<void> MainWindow::openAndPlayPlaylistAsync(QString sourceId, Playlist playlist)
{
    co_await showPlaylistAsync(sourceId, playlist);
    playCurrentPlaylist();
}

Rpc::Task<void> MainWindow::startRadioAsync(QString sourceId, QString seed)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    heroPanel_->setPlayBusy(true);
    try {
        StartRadioParams params { seed };
        StartRadioResult result = co_await Rpc::catalogStartRadio(*client, params);
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
    if (trackListModel_->isMixedSource()) {
        // History rows can come from different backends, and
        // PlaybackController::loadQueue takes one sourceId for the whole
        // queue — so replay just the clicked track instead of queuing the
        // rest of the list.
        playback_.loadQueue(trackListModel_->sourceIdAt(index.row()), { trackListModel_->trackAt(index.row()) }, 0);
        return;
    }
    playback_.loadQueue(trackListModel_->sourceId(), trackListModel_->allTracks(), index.row());
}

void MainWindow::onTrackContextMenuRequested(const QPoint& pos)
{
    const QModelIndex index = trackListView_->indexAt(pos);
    if (!index.isValid())
        return;
    const int row = index.row();
    const QString sourceId
        = trackListModel_->isMixedSource() ? trackListModel_->sourceIdAt(row) : trackListModel_->sourceId();
    const Track track = trackListModel_->trackAt(row);

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
    menu->addAction(Theme::icon(QStringLiteral("play_arrow"), Theme::IconColor::Ink, 16), tr("Play"), this,
        [this, index]() { onTrackDoubleClicked(index); });
    menu->addAction(Theme::icon(QStringLiteral("playlist_play"), Theme::IconColor::Ink, 16), tr("Play Next"), this,
        [this, sourceId, track]() { playback_.enqueueNext(sourceId, track); });
    menu->addAction(Theme::icon(QStringLiteral("playlist_add"), Theme::IconColor::Ink, 16), tr("Add to Queue"), this,
        [this, sourceId, track]() { playback_.enqueueAtEnd(sourceId, track); });

    if (likeSupported || dislikeSupported) {
        menu->addSeparator();
        if (likeSupported) {
            QAction* likeAction
                = menu->addAction(Theme::icon(QStringLiteral("thumb_up"), Theme::IconColor::Ink, 16), tr("Like"));
            likeAction->setCheckable(true);
            const bool liked = track.liked.value_or(false);
            likeAction->setChecked(liked);
            connect(likeAction, &QAction::triggered, this, [this, sourceId, id = track.id, liked]() {
                likeToggledAsync(sourceId, id, !liked, /*announceSuccess=*/true).detach();
            });
        }
        if (dislikeSupported) {
            // Not checkable, unlike Like — the protocol carries no
            // persisted "disliked" field on Track (see NowPlayingBar's
            // toolbar dislike button, which has the same limitation), so
            // there's no accurate checked state to seed this from. A
            // one-shot "mark as disliked" action instead.
            QAction* dislikeAction
                = menu->addAction(Theme::icon(QStringLiteral("thumb_down"), Theme::IconColor::Ink, 16), tr("Dislike"));
            connect(dislikeAction, &QAction::triggered, this, [this, sourceId, id = track.id]() {
                dislikeToggledAsync(sourceId, id, /*disliked=*/true, /*announceSuccess=*/true).detach();
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
            tr("Start Radio from This Track"), this,
            [this, sourceId, id = track.id]() { startRadioAsync(sourceId, id).detach(); });
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

    menu->popup(trackListView_->viewport()->mapToGlobal(pos));
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
        playback_.setTrackLiked(sourceId, trackId, liked);
        trackListModel_->markTrackLiked(sourceId, trackId, liked);
        playbackHistory_->markTrackLiked(sourceId, trackId, liked);
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
        if (disliked) {
            // Cross-clear Like the same way likeToggledAsync() does for
            // Dislike — see docs/protocol.md §7.4. Caches patched
            // unconditionally (see likeToggledAsync's stillCurrent() doc
            // comment); only the NowPlayingBar update below is gated.
            playback_.setTrackLiked(sourceId, trackId, false);
            trackListModel_->markTrackLiked(sourceId, trackId, false);
            playbackHistory_->markTrackLiked(sourceId, trackId, false);
        }
        if (stillCurrent()) {
            nowPlayingBar_->setDislikeState(dislikeSupported, disliked);
            if (disliked)
                nowPlayingBar_->setLikeState(likeSupported, false);
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
