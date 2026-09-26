#include "MainWindow.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QListView>
#include <QLoggingCategory>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QProgressBar>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QWindow>

#include <algorithm>
#include <functional>
#include <optional>

#include "AboutDialog.h"
#include "CoverArtCache.h"
#include "DownloadPaths.h"
#include "EmptyStatePlaceholder.h"
#include "HeroPanel.h"
#include "Icons.h"
#include "InfoDialog.h"
#include "MenuCheckRow.h"
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
#include "Typography.h"
#include "WindowGlass.h"

namespace Ui {

namespace {
// Warning-level, so it always shows regardless of --debug/CLOUDMUS_QT_DEBUG
// (see Logging.cpp's messageHandler) — every RPC/async failure this window
// surfaces to the user via a toast also gets logged here, so a report like
// "I clicked X and nothing happened" has something to look at in the
// console even if the toast was missed.
Q_LOGGING_CATEGORY(lcMainWindow, "cloudmus.ui.mainwindow")

// The narrowest the splitter panes go — below these the sidebar's rows,
// the hero panel and the track rows start to overlap and clip.
constexpr int kSidebarMinWidth = 134;
constexpr int kHeroMinWidth = 260;
constexpr int kTrackListMinWidth = 214;

// A station as a HeroPanel promotes it (the sheet's radio page, and the
// main screen before it starts): one without a description of its own
// (YouTube's "My Supermix") says what it is, instead of a bare title.
Playlist radioPromo(Playlist station)
{
    if (station.description.value_or(QString()).isEmpty())
        station.description = MainWindow::tr("A continuous radio station");
    return station;
}
} // namespace

MainWindow::MainWindow(App::Core& core, QWidget* parent)
    : QMainWindow(parent)
    , authStates_(&core.authStates())
    , sourceManager_(core.sourceManager())
    , playback_(core.playback())
    , settings_(core.settings())
    , sourceSession_(core.sourceSession())
    , messages_(core.messages())
    , nowPlaying_(core.nowPlaying())
    , coverArtCache_(&core.coverArtCache())
    , playbackHistory_(&core.playbackHistory())
    , trackStates_(&core.trackStates())
{
    setWindowTitle(QStringLiteral("CloudMus"));
    // Before the native window exists — it's created with an alpha channel
    // or not at all. paintEvent() paints the glass tint; showEvent() asks
    // for the blur behind it.
    setAttribute(Qt::WA_TranslucentBackground, Theme::glassEnabled());
    connect(&Theme::notifier(), &Theme::Notifier::glassChanged, this, &MainWindow::applyGlass);
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

    // --- now-playing controls, merged into the bottom toolbar alongside
    // the hamburger menu. No cover art here — HeroPanel (below) shows
    // whatever's playing instead, so it isn't duplicated. ---
    nowPlayingBar_ = new NowPlayingBar(this);

    connect(&playback_, &Playback::PlaybackController::queueChanged, this, &MainWindow::refreshMainList);

    connect(playbackHistory_, &History::PlaybackHistory::changed, this, [this]() {
        // Refresh in place — a track just started playing.
        if (sheetContext_.isHistory && sheet_->isPresented())
            fillHistorySheet();
    });

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
    menu->addAction(tr("Settings…"), this, [this]() { showSettingsDialog(); });
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
    ScrollEdgeFade::attach(
        sidebarView_, [] { return Theme::glass(Theme::palette().surface100, Theme::kChromeGlassOpacity); });
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
    // Rows are expanded as they come in — setSource() rebuilds a source's
    // rows on every refresh — except the ones the user collapsed, which
    // stay collapsed across refreshes and restarts.
    const QStringList collapsed = settings_.sidebarCollapsed();
    collapsedNodes_ = QSet<QString>(collapsed.cbegin(), collapsed.cend());
    connect(sidebarModel_, &QStandardItemModel::rowsInserted, this, &MainWindow::restoreExpansion);
    const auto rememberExpansion = [this](const QModelIndex& index, bool expanded) {
        const QString key = SidebarModel::nodeKey(index);
        if (key.isEmpty() || collapsedNodes_.contains(key) == !expanded)
            return;
        if (expanded)
            collapsedNodes_.remove(key);
        else
            collapsedNodes_.insert(key);
        settings_.setSidebarCollapsed(QStringList(collapsedNodes_.cbegin(), collapsedNodes_.cend()));
    };
    connect(sidebarView_, &QTreeView::expanded, this,
        [rememberExpansion](const QModelIndex& index) { rememberExpansion(index, true); });
    connect(sidebarView_, &QTreeView::collapsed, this,
        [rememberExpansion](const QModelIndex& index) { rememberExpansion(index, false); });
    connect(sidebarView_, &QTreeView::clicked, this, &MainWindow::onSidebarActivated);
    connect(sidebarView_, &QTreeView::doubleClicked, this, &MainWindow::onSidebarDoubleClicked);
    sidebarView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(sidebarView_, &QTreeView::customContextMenuRequested, this, &MainWindow::onSidebarContextMenuRequested);

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
    connect(
        coverArtCache_, &Covers::CoverArtCache::pixmapReady, this, [this]() { trackListView_->viewport()->update(); });
    trackListView_ = new QListView(this);
    trackListView_->setObjectName(QStringLiteral("trackListView")); // see StyleSheet.cpp's trackListBlock()
    trackListView_->setModel(trackListModel_);
    trackListView_->setItemDelegate(trackRowDelegate_);
    // Needed for State_MouseOver to be set at all — see the delegate's
    // hover-only play button drawn over the cover thumbnail.
    trackListView_->setMouseTracking(true);
    trackListView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    SmoothScroller::attach(trackListView_);
    ScrollEdgeFade::attach(trackListView_, [] { return Theme::glass(Theme::palette().surface0); });
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
    // Neither pane can be dragged shut or squeezed so far that the track
    // rows' and the hero's layouts break (setTrackListVisible() hides the
    // list outright instead).
    contentSplitter_->setChildrenCollapsible(false);
    heroPanel_->setMinimumWidth(kHeroMinWidth);
    trackListPane_->setMinimumWidth(kTrackListMinWidth);
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
    connect(sourcePanel_, &SourcePanel::favoriteToggled, this, &MainWindow::toggleFavorite);
    sourcePanel_->setFavoriteCheck([this](const QString& sourceId, const QString& playlistId) {
        return sidebarModel_->isFavorite(sourceId, playlistId);
    });
    connect(sourcePanel_, &SourcePanel::codeCopied, this, [this]() { messages_.success(tr("Code copied")); });
    connect(sourcePanel_, &SourcePanel::settingsRequested, this,
        [this](const QString& sourceId) { showSettingsDialog(QStringLiteral("source:") + sourceId); });
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
        pendingSelection_.clear();
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
    // Same for the sidebar: never dragged shut, never narrower than its
    // rows' icon + title + status icon need.
    splitter->setChildrenCollapsible(false);
    sidebarView_->setMinimumWidth(kSidebarMinWidth);
    splitter->setSizes({ settings_.sidebarWidth(), 720 });
    connect(splitter, &QSplitter::splitterMoved, this,
        [this, splitter]() { settings_.setSidebarWidth(splitter->sizes().first()); });

    toastNotifier_ = new ToastNotifier(this);
    // Whatever the core has to tell the user shows as a toast — on
    // whichever notifier is current (the Settings dialog's while it's up).
    connect(
        &messages_, &ViewModel::Messages::posted, this, [this](ViewModel::Messages::Kind kind, const QString& text) {
            switch (kind) {
                case ViewModel::Messages::Kind::Info:
                    toastNotifier_->showInfo(text);
                    break;
                case ViewModel::Messages::Kind::Success:
                    toastNotifier_->showSuccess(text);
                    break;
                case ViewModel::Messages::Kind::Error:
                    toastNotifier_->showError(text);
                    break;
            }
        });

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(splitter, 1);
    setCentralWidget(central);

    // Every backend gets its header row as soon as its process is spawned
    // (sourceManager_.startAll() runs after MainWindow is constructed — see
    // main.cpp), not once loadPlaylistsAsync() first succeeds — a backend
    // that's slow to start or still authenticating must stay visible. Shown
    // as loading until loadPlaylistsAsync() (on SourceSession::sourceReady)
    // fills it in.
    connect(&sourceManager_, &Rpc::SourceManager::sourceStarting, this, [this](const Rpc::BackendManifest& manifest) {
        sidebarModel_->setSourceIconPath(manifest.id, manifest.iconPath);
        sidebarModel_->setFavorites(manifest.id, settings_.favorites(manifest.id));
        sidebarModel_->setSource(manifest.id, manifest.name, { });
        sidebarModel_->setSourceLoading(manifest.id, manifest.name, true);
    });
    connect(&sourceManager_, &Rpc::SourceManager::sourceStopped, this, &MainWindow::onSourceStopped);
    connect(authStates_, &Rpc::AuthStates::changed, this, &MainWindow::updateSourceAuthIndicator);
    connect(&sourceManager_, &Rpc::SourceManager::settingsChanged, this, [this](const QString& sourceId) {
        if (Rpc::RpcClient* client = sourceManager_.client(sourceId))
            loadPlaylistsAsync(client).detach();
    });
    // auth.logout (from the source's Settings page) leaves the playlists
    // listed until something reloads them; they're no longer the user's to see.
    connect(authStates_, &Rpc::AuthStates::signedOut, this, [this](const QString& sourceId) {
        if (Rpc::RpcClient* client = sourceManager_.client(sourceId)) {
            sidebarModel_->setSource(sourceId, client->sourceName(), { });
            updateSourceAuthIndicator(sourceId);
            if (currentStatusPanelSourceId_ == sourceId)
                sourcePanel_->setPlaylists({ }, false);
            syncSidebarSelection();
        }
    });
    // App::SourceSession has wired the source up by now (it runs first):
    // all that's left for the window is listing what it offers.
    connect(&sourceSession_, &App::SourceSession::sourceReady, this,
        [this](Rpc::RpcClient* client) { loadPlaylistsAsync(client).detach(); });
    connect(&sourceSession_, &App::SourceSession::signedIn, this,
        [this](Rpc::RpcClient* client) { loadPlaylistsAsync(client).detach(); });
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
    // And the page that was open in the sidebar, the same way.
    pendingSelection_ = settings_.sidebarSelection();
    if (pendingSelection_ == QStringLiteral("history"))
        QTimer::singleShot(0, this, [this]() { restoreSelection(QString(), { }); });

    bindNowPlaying();
}

void MainWindow::bindNowPlaying()
{
    // What the user does with the toolbar goes to the view model...
    using ViewModel::NowPlaying;
    connect(nowPlayingBar_, &NowPlayingBar::playPauseClicked, &nowPlaying_, &NowPlaying::togglePause);
    connect(nowPlayingBar_, &NowPlayingBar::nextClicked, &nowPlaying_, &NowPlaying::next);
    connect(nowPlayingBar_, &NowPlayingBar::previousClicked, &nowPlaying_, &NowPlaying::previous);
    connect(nowPlayingBar_, &NowPlayingBar::stopClicked, &nowPlaying_, &NowPlaying::stop);
    connect(nowPlayingBar_, &NowPlayingBar::seekRequested, &nowPlaying_, &NowPlaying::seek);
    connect(nowPlayingBar_, &NowPlayingBar::volumeChanged, &nowPlaying_, &NowPlaying::setVolume);
    connect(nowPlayingBar_, &NowPlayingBar::shuffleClicked, &nowPlaying_, &NowPlaying::setShuffle);
    connect(nowPlayingBar_, &NowPlayingBar::repeatClicked, &nowPlaying_, &NowPlaying::setRepeatMode);
    connect(nowPlayingBar_, &NowPlayingBar::likeClicked, &nowPlaying_, &NowPlaying::setLiked);
    connect(nowPlayingBar_, &NowPlayingBar::dislikeClicked, &nowPlaying_, &NowPlaying::setDisliked);
    connect(nowPlayingBar_, &NowPlayingBar::downloadClicked, &nowPlaying_, &NowPlaying::download);
    connect(nowPlayingBar_, &NowPlayingBar::playlistsClicked, this, &MainWindow::showPlaylistsMenu);

    // ...and what it says is shown: the toolbar, the hero panel (the
    // playing track; once playback stops, the active playlist again) and
    // the playing row in both track lists.
    const auto showTrack = [this]() {
        const bool hasTrack = nowPlaying_.hasTrack();
        nowPlayingBar_->setTrackAvailable(hasTrack);
        nowPlayingBar_->setTrackWebUrl(nowPlaying_.webUrl());
        if (hasTrack)
            heroPanel_->setNowPlaying(nowPlaying_.track());
        else
            refreshHero();
        const QString sourceId = nowPlaying_.sourceId();
        const QString trackId = hasTrack ? nowPlaying_.track().id : QString();
        trackRowDelegate_->setCurrentlyPlaying(sourceId, trackId);
        trackListView_->viewport()->update();
        sheet_->trackDelegate()->setCurrentlyPlaying(sourceId, trackId);
        sheet_->updateRows();
    };
    const auto showFeedback = [this]() {
        const NowPlaying::Feedback feedback = nowPlaying_.feedback();
        // set…State() clears busy — busy goes on after it.
        nowPlayingBar_->setLikeState(feedback.likeSupported, feedback.liked);
        nowPlayingBar_->setLikeBusy(feedback.likeBusy);
        nowPlayingBar_->setDislikeState(feedback.dislikeSupported, feedback.disliked);
        nowPlayingBar_->setDislikeBusy(feedback.dislikeBusy);
        nowPlayingBar_->setDownloadState(feedback.downloadSupported);
        nowPlayingBar_->setDownloadBusy(feedback.downloadBusy);
        nowPlayingBar_->setPlaylistsState(feedback.playlistsSupported);
    };
    const auto showPlayModes = [this]() {
        nowPlayingBar_->setPlayModes(nowPlaying_.shuffle(), nowPlaying_.repeatMode(), nowPlaying_.isRadio());
    };
    connect(&nowPlaying_, &NowPlaying::trackChanged, this, showTrack);
    connect(&nowPlaying_, &NowPlaying::feedbackChanged, this, showFeedback);
    connect(&nowPlaying_, &NowPlaying::playModesChanged, this, showPlayModes);
    connect(&nowPlaying_, &NowPlaying::playingChanged, nowPlayingBar_, &NowPlayingBar::setPlaying);
    connect(&nowPlaying_, &NowPlaying::loadingChanged, nowPlayingBar_, &NowPlayingBar::setLoading);
    connect(&nowPlaying_, &NowPlaying::positionChanged, nowPlayingBar_, &NowPlayingBar::setPosition);
    connect(&nowPlaying_, &NowPlaying::queueAvailabilityChanged, nowPlayingBar_, &NowPlayingBar::setQueueAvailable);

    // Whatever state it's in already — a window created while something
    // plays shows it at once, not from the next change on.
    nowPlayingBar_->setVolume(nowPlaying_.volume());
    nowPlayingBar_->setPlaying(nowPlaying_.playing());
    if (nowPlaying_.loading())
        nowPlayingBar_->setLoading(true);
    nowPlayingBar_->setQueueAvailable(nowPlaying_.queueAvailable());
    showTrack();
    showFeedback();
    showPlayModes();
}

MainWindow::~MainWindow() { settings_.setWindowGeometry(saveGeometry()); }

Rpc::Task<void> MainWindow::retryAuthAsync(QString sourceId)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    sourcePanel_->setAuthActionBusy(true);
    co_await sourceSession_.signIn(sourceId);
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
    const Rpc::AuthStates::State state = authStates_->state(sourceId);
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

    refreshAuthSection(sourceId, authStates_->state(sourceId));
    // Last: presenting snapshots the sheet, so fill it first.
    sheet_->present();
}

void MainWindow::refreshAuthSection(const QString& sourceId, const Rpc::AuthStates::State& state)
{
    Q_UNUSED(sourceId);
    if (!state.prompt.isEmpty()) {
        sourcePanel_->showPrompt(state.prompt);
    } else if (!state.errorMessage.isEmpty()) {
        sourcePanel_->showError(state.errorMessage);
    } else if (state.hasProblem) {
        // Signed out on purpose: nothing's gone wrong, it just waits for a
        // sign-in to be asked for.
        sourcePanel_->showSignInNeeded();
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
    messages_.error(tr("%1 is unavailable").arg(name));
}

void MainWindow::onSourceStopped(const QString& sourceId)
{
    sidebarModel_->removeSource(sourceId);
    authStates_->remove(sourceId);
    if (sheet_->isPresented()
        && (currentStatusPanelSourceId_ == sourceId
            || (sheetContext_.isValid() && !sheetContext_.isHistory && sheetContext_.sourceId == sourceId)))
        closeSheet();
    // Nothing left to stream the rest of the track from.
    if (playback_.hasCurrentTrack() && playback_.currentSourceId() == sourceId)
        playback_.stop();
    syncSidebarSelection();
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
            // completes, see SourceSession::signedIn).
            qCWarning(lcMainWindow) << "catalog.listPlaylists failed for" << client->sourceId() << ":" << e.what();
        }
    }
    for (const Playlist& playlist : playlists)
        coverArtCache_->assignSource(client->sourceId(), playlist.coverUrl.value_or(QString()));
    sidebarModel_->setSource(client->sourceId(), client->sourceName(), playlists);
    // setSource() just recreated this source's header row from scratch,
    // dropping any warning/loading/error icon it had — reapply from the
    // cached state. Needed because this coroutine and the auth.start flow
    // kicked off alongside it in App::SourceSession race: an auth/prompt can
    // arrive and set the icon before this RPC round-trip finishes, in which
    // case this call would otherwise silently wipe it back off.
    updateSourceAuthIndicator(client->sourceId());
    sidebarModel_->setSourceLoading(client->sourceId(), client->sourceName(), false);
    sidebarModel_->setSourceFetchError(client->sourceId(), client->sourceName(), fetchTimedOut);
    if (fetchTimedOut) {
        messages_.error(tr("%1: timed out loading playlists").arg(client->sourceName()));
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
    restoreSelection(client->sourceId(), playlists);
    // setSource() rebuilt this source's rows, dropping their selection.
    syncSidebarSelection();
    if (currentStatusPanelSourceId_ == client->sourceId())
        sourcePanel_->setPlaylists(playlists, /*loading=*/false);
    // Which ones are favorites can change with the list (Playlist.featured).
    sourcePanel_->favoritesChanged();
}

void MainWindow::onSidebarActivated(const QModelIndex& index)
{
    pendingSelection_.clear(); // the user picked something else meanwhile
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
    pendingSelection_.clear();
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
        const QString playlistId = index.data(SidebarModel::PlaylistIdRole).toString();
        const bool favorite = sidebarModel_->isFavorite(sourceId, playlistId);
        menu->addAction(
            Theme::icon(favorite ? QStringLiteral("star_border") : QStringLiteral("star"), Theme::IconColor::Ink, 16),
            favorite ? tr("Remove from Favorites") : tr("Add to Favorites"), this,
            [this, sourceId, playlistId]() { toggleFavorite(sourceId, playlistId); });
    } else {
        menu->deleteLater();
        return;
    }

    menu->popup(sidebarView_->viewport()->mapToGlobal(pos));
}

void MainWindow::toggleFavorite(const QString& sourceId, const QString& playlistId)
{
    // The first change turns the source's own suggestion into the user's
    // list, so it no longer follows Playlist.featured.
    QStringList favorites = sidebarModel_->favoritesFor(sourceId);
    const bool removed = favorites.removeOne(playlistId);
    if (!removed)
        favorites.append(playlistId);
    settings_.setFavorites(sourceId, favorites);
    sidebarModel_->setFavorites(sourceId, favorites);
    // The rows were rebuilt, dropping their selection.
    syncSidebarSelection();
    sourcePanel_->favoritesChanged();

    // A station (or Liked) taken out of the favorites leaves the sidebar
    // altogether — say once where it went, or it looks gone for good.
    if (!removed || settings_.hiddenFavoriteHintShown())
        return;
    const QList<Playlist> playlists = sidebarModel_->playlistsFor(sourceId);
    const auto it
        = std::find_if(playlists.cbegin(), playlists.cend(), [&](const Playlist& p) { return p.id == playlistId; });
    if (it == playlists.cend() || it->kind == QStringLiteral("playlist"))
        return;
    settings_.setHiddenFavoriteHintShown();
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    const QString sourceName = client != nullptr ? client->sourceName() : sourceId;
    InfoDialog dialog(tr("Removed from Favorites"), tr("“%1” is no longer in the sidebar").arg(it->title),
        tr("Only favorites are shown in the sidebar, apart from a source's regular playlists. Everything "
           "else — stations, mixes, liked tracks — is listed on the source's page: click “%1” in the sidebar, "
           "and use the star next to an entry to bring it back.")
            .arg(sourceName),
        tr("Open %1").arg(sourceName), this);
    if (dialog.exec() == QDialog::Accepted)
        showSourceStatusPanel(sourceId);
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
    coverArtCache_->assignSource(sourceId, tracks);
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
        messages_.error(QString::fromStdString(e.what()));
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
    heroPanel_->setPlaylist(activeContext_.isRadio() ? radioPromo(activeContext_.playlist) : activeContext_.playlist);
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

    // Remembered for the next start — but not while the last run's is
    // still waiting for its source to load: what's selected until then
    // (nothing, or the restored active playlist) isn't the user's choice.
    if (pendingSelection_.isEmpty())
        settings_.setSidebarSelection(selectionKey());
}

QString MainWindow::selectionKey() const
{
    const ActiveContext& context = sheet_->isPresented() ? sheetContext_ : activeContext_;
    if (sheet_->isPresented() && !context.isValid())
        return currentStatusPanelSourceId_.isEmpty() ? QString()
                                                     : QStringLiteral("source:") + currentStatusPanelSourceId_;
    if (!context.isValid())
        return QString();
    if (context.isHistory)
        return QStringLiteral("history");
    return QStringLiteral("playlist:%1:%2").arg(context.sourceId, context.playlist.id);
}

void MainWindow::restoreSelection(const QString& sourceId, const QList<Playlist>& playlists)
{
    const QString key = pendingSelection_;
    if (key.isEmpty())
        return;
    if (key == QStringLiteral("history")) {
        if (!sourceId.isEmpty())
            return;
        pendingSelection_.clear();
        if (!activeContext_.isHistory)
            openHistoryInSheet();
        return;
    }
    if (key == QStringLiteral("source:") + sourceId) {
        pendingSelection_.clear();
        showSourceStatusPanel(sourceId);
        return;
    }
    const QString prefix = QStringLiteral("playlist:%1:").arg(sourceId);
    if (!key.startsWith(prefix))
        return;
    pendingSelection_.clear();
    const QString playlistId = key.mid(prefix.size());
    for (const Playlist& playlist : playlists) {
        if (playlist.id != playlistId)
            continue;
        // The active playlist is already in the main area — selecting it
        // is all there is to do, and syncSidebarSelection() does that.
        if (!activeContext_.sameAs(ActiveContext { sourceId, playlist }))
            openInSheetAsync(sourceId, playlist).detach();
        break;
    }
    syncSidebarSelection();
}

void MainWindow::restoreExpansion(const QModelIndex& parent, int first, int last)
{
    const std::function<void(const QModelIndex&)> apply = [&](const QModelIndex& index) {
        const QString key = SidebarModel::nodeKey(index);
        if (!key.isEmpty())
            sidebarView_->setExpanded(index, !collapsedNodes_.contains(key));
        for (int row = 0; row < sidebarModel_->rowCount(index); ++row)
            apply(sidebarModel_->index(row, 0, index));
    };
    // The parent too: a row can get its first children only now, and a
    // view won't keep a childless row expanded.
    if (parent.isValid())
        apply(parent);
    else
        for (int row = first; row <= last; ++row)
            apply(sidebarModel_->index(row, 0, parent));
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
        sheet_->showRadio(radioPromo(playlist));
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
        messages_.error(QString::fromStdString(e.what()));
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
        coverArtCache_->assignSource(sourceId, result.initialTracks);
        // Before startRadio(), so the queue it emits lands in the main
        // list under the right playlist.
        setActiveContext(context);
        playback_.startRadio(sourceId, result.stationId, result.initialTracks);
    } catch (const std::exception& e) {
        qCWarning(lcMainWindow) << "starting radio failed for" << sourceId << ":" << e.what();
        messages_.error(QString::fromStdString(e.what()));
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
    if (sourceCanEditPlaylists(sourceId)) {
        QMenu* playlists
            = menu->addMenu(Theme::icon(QStringLiteral("playlist_add"), Theme::IconColor::Ink, 16), tr("Playlists"));
        // Filled on first open only — getTrackPlaylists is a round-trip.
        connect(playlists, &QMenu::aboutToShow, this, [this, playlists, sourceId, track]() {
            if (playlists->property("filled").toBool())
                return;
            playlists->setProperty("filled", true);
            fillPlaylistsMenuAsync(playlists, sourceId, track).detach();
        });
    }

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
                nowPlaying_.setTrackLiked(sourceId, id, !liked, /*announce=*/true).detach();
            });
        }
        if (dislikeSupported) {
            const bool disliked = trackStates_->state(sourceId, track.id).disliked.value_or(false);
            QAction* dislikeAction
                = menu->addAction(Theme::icon(QStringLiteral("heart_broken"),
                                      disliked ? Theme::IconColor::Accent : Theme::IconColor::Ink, 16),
                    disliked ? tr("Remove Dislike") : tr("Dislike"));
            connect(dislikeAction, &QAction::triggered, this, [this, sourceId, id = track.id, disliked]() {
                nowPlaying_.setTrackDisliked(sourceId, id, !disliked, /*announce=*/true).detach();
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
                [this, sourceId, track]() { nowPlaying_.downloadTrack(sourceId, track).detach(); });
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
    const QString error = co_await sourceSession_.submitSignIn(sourceId, fields);
    if (!error.isEmpty())
        sourcePanel_->showError(error);
    sourcePanel_->setAuthActionBusy(false);
}

void MainWindow::fillNowPlayingPlaylistsMenu(QMenu* menu)
{
    menu->clear();
    if (!playback_.hasCurrentTrack())
        return;
    fillPlaylistsMenuAsync(menu, playback_.currentSourceId(), playback_.currentTrack(), std::nullopt,
        /*checkActions=*/true)
        .detach();
}

bool MainWindow::isOnScreen() const
{
    return isVisible() && !isMinimized() && (windowHandle() == nullptr || windowHandle()->isExposed());
}

void MainWindow::bringToFront(const QString& activationToken)
{
    // Qt's Wayland backend picks the token up from this variable when the
    // window requests activation.
    if (!activationToken.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
    // A window hidden to the tray is mapped anew, and the window manager
    // places it like a new one (centered on the screen under the mouse),
    // so put it back on its own screen and spot. X11 only: on Wayland a
    // client can't position its windows — that takes the compositor's
    // session restore (xdg-session-management), which Qt doesn't speak yet.
    if (!trayHiddenGeometry_.isEmpty() && !isVisible() && QGuiApplication::platformName() == QLatin1String("xcb"))
        restoreGeometry(trayHiddenGeometry_);
    trayHiddenGeometry_.clear();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    show();
    raise();
    activateWindow();
}

void MainWindow::toggleShown()
{
    if (isOnScreen())
        hideToTray();
    else
        bringToFront();
}

void MainWindow::hideToTray()
{
    trayHiddenGeometry_ = saveGeometry();
    hide();
}

bool MainWindow::sourceCanEditPlaylists(const QString& sourceId) const
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    return client != nullptr
        && client->capabilities()
               .value(QStringLiteral("browse"))
               .toObject()
               .value(QStringLiteral("editPlaylists"))
               .toBool();
}

void MainWindow::showPlaylistsMenu(QPoint anchor)
{
    if (!playback_.hasCurrentTrack())
        return;
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    fillPlaylistsMenuAsync(menu, playback_.currentSourceId(), playback_.currentTrack(), anchor).detach();
    menu->popup(anchor);
}

Rpc::Task<void> MainWindow::fillPlaylistsMenuAsync(
    QPointer<QMenu> menu, QString sourceId, Track track, std::optional<QPoint> reopenAt, bool checkActions)
{
    const auto showNote = [&menu](const QString& text) {
        menu->clear();
        menu->addAction(text)->setEnabled(false);
    };
    showNote(tr("Loading…"));

    const QList<Playlist> playlists = sidebarModel_->editablePlaylistsFor(sourceId);
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (playlists.isEmpty() || client == nullptr) {
        showNote(tr("No playlists"));
        co_return;
    }
    QStringList containing;
    try {
        containing
            = (co_await Rpc::catalogGetTrackPlaylists(*client, GetTrackPlaylistsParams { track.id })).playlistIds;
    } catch (const std::exception& e) {
        qCWarning(lcMainWindow) << "catalog.getTrackPlaylists failed for" << track.id << ":" << e.what();
        if (menu)
            showNote(tr("Couldn't load playlists"));
        co_return;
    }
    if (!menu)
        co_return; // closed meanwhile

    menu->clear();
    for (const Playlist& playlist : playlists) {
        if (checkActions) {
            QAction* action = menu->addAction(playlist.title);
            action->setCheckable(true);
            action->setChecked(containing.contains(playlist.id));
            connect(action, &QAction::toggled, this, [this, sourceId, track, playlist](bool checked) {
                setTrackInPlaylistAsync(sourceId, track, playlist, checked).detach();
            });
            continue;
        }
        // A check box row rather than a checkable QAction: toggling one
        // doesn't close the menu, so several playlists can be changed in
        // one go — see MenuCheckRow.
        auto* row = new MenuCheckRow(playlist.title, menu);
        QCheckBox* box = row->checkBox();
        box->setChecked(containing.contains(playlist.id));
        auto* action = new QWidgetAction(menu);
        action->setDefaultWidget(row);
        menu->addAction(action);
        connect(box, &QCheckBox::toggled, this, [this, sourceId, track, playlist, box](bool checked) {
            setTrackInPlaylistAsync(sourceId, track, playlist, checked, box).detach();
        });
    }
    if (reopenAt && menu->isVisible())
        menu->popup(*reopenAt); // grew past "Loading…" — keep it on screen
}

Rpc::Task<void> MainWindow::setTrackInPlaylistAsync(
    QString sourceId, Track track, Playlist playlist, bool add, QPointer<QCheckBox> box)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    if (box)
        box->setEnabled(false); // one request at a time per playlist
    try {
        int trackCount = 0;
        if (add)
            trackCount = (co_await Rpc::catalogAddToPlaylist(*client, AddToPlaylistParams { playlist.id, track.id }))
                             .trackCount;
        else
            trackCount
                = (co_await Rpc::catalogRemoveFromPlaylist(*client, RemoveFromPlaylistParams { playlist.id, track.id }))
                      .trackCount;
        applyPlaylistEdit(sourceId, track, playlist.id, add, trackCount);
        messages_.info(add ? tr("Added to \"%1\"").arg(playlist.title) : tr("Removed from \"%1\"").arg(playlist.title));
    } catch (const std::exception& e) {
        const QString message = QString::fromStdString(e.what());
        qCWarning(lcMainWindow) << "playlist edit failed for" << playlist.id << ":" << message;
        messages_.error(tr("%1: %2").arg(client->sourceName(), message));
        if (box) {
            const QSignalBlocker blocker(box);
            box->setChecked(!add); // back to how it really is
        }
    }
    if (box)
        box->setEnabled(true);
}

void MainWindow::applyPlaylistEdit(
    const QString& sourceId, const Track& track, const QString& playlistId, bool added, int trackCount)
{
    sidebarModel_->setPlaylistTrackCount(sourceId, playlistId, trackCount);

    const auto isThatPlaylist = [&](const ActiveContext& context) {
        return !context.isHistory && context.sourceId == sourceId && context.playlist.id == playlistId;
    };
    if (isThatPlaylist(sheetContext_)) {
        sheetContext_.playlist.trackCount = trackCount;
        if (added)
            sheet_->trackModel()->appendEntry(sourceId, track);
        else
            sheet_->trackModel()->removeFirst(sourceId, track.id);
        sheet_->setSubtitle(trackCountText(trackCount));
    }
    if (isThatPlaylist(activeContext_)) {
        activeContext_.playlist.trackCount = trackCount;
        // Only the not-yet-queued tracks — a running queue stays as it is.
        if (!playback_.hasQueue()) {
            if (added) {
                activeTracks_.append(Playback::QueueEntry { sourceId, track });
            } else {
                for (int i = 0; i < activeTracks_.size(); ++i) {
                    if (activeTracks_[i].track.id == track.id) {
                        activeTracks_.removeAt(i);
                        break;
                    }
                }
            }
            refreshMainList();
        }
    }
}

void MainWindow::showAboutDialog() { Ui::AboutDialog(this).exec(); }

void MainWindow::showSettingsDialog(const QString& openAt)
{
    SettingsDialog dialog(settings_, sourceManager_, *authStates_, this, openAt);
    // While it's up, this window's toasts (an auth error from App::SourceSession,
    // a download finishing) go to the dialog instead: this window is
    // behind it, where they'd go unseen. Restored before the dialog, and
    // its notifier with it, is destroyed.
    ToastNotifier* const ownToasts = toastNotifier_;
    toastNotifier_ = dialog.toastNotifier();
    dialog.exec();
    toastNotifier_ = ownToasts;
    // A glass change from the General page, applied only now: switching
    // it recreates this window's native window (applyGlass()), not
    // something to do under a modal dialog parented to it.
    Theme::setGlassEnabled(Theme::glassWanted(settings_.glassBackground()) && Integration::WindowGlass::available());
}

void MainWindow::quitForReal()
{
    reallyQuitting_ = true;
    close();
}

void MainWindow::applyGlass()
{
    // Switched from Settings: recreate just the native window, now with an
    // alpha channel or without — every widget, and playback, carry on.
    // setWindowFlags() is what makes Qt drop and recreate it; it also
    // hides the window, so its place and state are put back after.
    const bool wasVisible = isVisible();
    const QByteArray geometry = saveGeometry();
    setAttribute(Qt::WA_TranslucentBackground, Theme::glassEnabled());
    setWindowFlags(windowFlags());
    restoreGeometry(geometry);
    if (wasVisible)
        show(); // showEvent() asks for the blur again, if glass is on
}

void MainWindow::paintEvent(QPaintEvent* event)
{
    // The glass tint behind all the chrome — painted here rather than as a
    // stylesheet background, which Qt skips on a translucent window.
    if (Theme::glassEnabled()) {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(event->rect(), Theme::glass(Theme::palette().surface100, Theme::kChromeGlassOpacity));
    }
    QMainWindow::paintEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    // On every show, not once: hiding to the tray can take the native
    // surface — and the blur set on it — away with it.
    if (Theme::glassEnabled())
        Integration::WindowGlass::enableBlurBehind(this);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!reallyQuitting_ && settings_.closeMinimizesToTray()) {
        event->ignore();
        hideToTray();
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
