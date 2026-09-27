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
#include "DownloadsPanel.h"
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
#include "TrackFetch.h"
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
    , downloads_(core.downloads())
    , playlistEditing_(core.playlistEditing())
    , sources_(core.sources())
    , activePlaylist_(core.activePlaylist())
    , browse_(core.browse())
    , sourcePage_(core.sourcePage())
    , coverArtCache_(&core.coverArtCache())
    , playbackHistory_(&core.playbackHistory())
    , trackStates_(&core.trackStates())
{
    setWindowTitle(QStringLiteral("CloudMus"));
    // Before the native window exists — it's created with an alpha channel
    // or not at all (switching glass takes a new window, see
    // Ui::WindowHost). paintEvent() paints the glass tint; showEvent() asks
    // for the blur behind it.
    setAttribute(Qt::WA_TranslucentBackground, Theme::glassEnabled());
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
    // Owned by ViewModel::Sources, which keeps it filled.
    sidebarModel_ = &sources_.model();
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
    // stay collapsed across refreshes and restarts (ViewModel::Sources
    // keeps which).
    connect(sidebarModel_, &QStandardItemModel::rowsInserted, this, &MainWindow::restoreExpansion);
    // Rows already there (a window created while the app runs) too.
    if (sidebarModel_->rowCount() > 0)
        restoreExpansion(QModelIndex(), 0, sidebarModel_->rowCount() - 1);
    connect(sidebarView_, &QTreeView::expanded, this,
        [this](const QModelIndex& index) { sources_.setCollapsed(ViewModel::SidebarModel::nodeKey(index), false); });
    connect(sidebarView_, &QTreeView::collapsed, this,
        [this](const QModelIndex& index) { sources_.setCollapsed(ViewModel::SidebarModel::nodeKey(index), true); });
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
    connect(heroPanel_, &HeroPanel::playClicked, &activePlaylist_, &ViewModel::ActivePlaylist::play);
    heroPanel_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(heroPanel_, &HeroPanel::customContextMenuRequested, this, &MainWindow::onHeroContextMenuRequested);

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
    connect(sourcePanel_, &SourcePanel::submitRequested, &sourcePage_, &ViewModel::SourcePage::submit);
    connect(sourcePanel_, &SourcePanel::retryRequested, &sourcePage_, &ViewModel::SourcePage::signIn);
    connect(sourcePanel_, &SourcePanel::playlistActivated, &browse_, &ViewModel::Browse::openPlaylist);
    connect(sourcePanel_, &SourcePanel::favoriteToggled, this, &MainWindow::toggleFavorite);
    sourcePanel_->setFavoriteCheck([this](const QString& sourceId, const QString& playlistId) {
        return sources_.isFavorite(sourceId, playlistId);
    });
    connect(sourcePanel_, &SourcePanel::codeCopied, this, [this]() { messages_.success(tr("Code copied")); });
    connect(sourcePanel_, &SourcePanel::settingsRequested, this,
        [this](const QString& sourceId) { showSettingsDialog(QStringLiteral("source:") + sourceId); });
    connect(sourcePanel_, &SourcePanel::refreshRequested, &sources_, &ViewModel::Sources::refresh);
    connect(sheet_, &PlaylistSheet::trackActivated, &browse_, &ViewModel::Browse::playRow);
    connect(sheet_, &PlaylistSheet::playAllClicked, &browse_, &ViewModel::Browse::playAll);
    connect(sheet_, &PlaylistSheet::downloadAllClicked, this, [this]() {
        if (browse_.page() == ViewModel::Browse::Page::Playlist)
            downloads_.downloadPlaylist(browse_.context().sourceId, browse_.context().playlist);
    });
    connect(sheet_, &PlaylistSheet::closeRequested, &browse_, &ViewModel::Browse::close);
    connect(sheet_, &PlaylistSheet::trackContextMenuRequested, this, [this](int row, const QPoint& globalPos) {
        TrackListModel* model = sheet_->trackModel();
        showTrackMenu(model->sourceIdAt(row), model->trackAt(row), globalPos, [this, row]() { browse_.playRow(row); });
    });
    // Dismissed however it was (Escape, a click outside): nothing's open.
    connect(sheet_, &PlaylistSheet::dismissed, &browse_, &ViewModel::Browse::close);

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

    // ViewModel::Sources follows the sources and what they list; the window
    // keeps its own views of them in step.
    connect(&sources_, &ViewModel::Sources::sourceChanged, this, &MainWindow::onSourceChanged);
    connect(&sources_, &ViewModel::Sources::playlistsLoaded, this, &MainWindow::syncSidebarSelection);
    connect(&sources_, &ViewModel::Sources::sourceRemoved, this, &MainWindow::syncSidebarSelection);
    connect(authStates_, &Rpc::AuthStates::changed, this, &MainWindow::updateSourceAuthIndicator);

    bindNowPlaying();
    bindActivePlaylist();
    bindBrowse();
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
    // Downloads under way: the button opens their panel; otherwise it
    // saves the playing track.
    connect(nowPlayingBar_, &NowPlayingBar::downloadClicked, this, [this](QPoint anchor) {
        if (downloads_.isActive())
            (new DownloadsPanel(downloads_, nowPlaying_, this))->popup(anchor);
        else
            nowPlaying_.download();
    });
    const auto showDownloads = [this]() {
        nowPlayingBar_->setDownloadsVisible(downloads_.isEnabled());
        nowPlayingBar_->setDownloadActivity(downloads_.isActive(), downloads_.progress());
    };
    connect(&downloads_, &ViewModel::Downloads::changed, this, showDownloads);
    // Downloads switched on or off: the open playlist's Save button follows.
    connect(&downloads_, &ViewModel::Downloads::changed, this, [this]() {
        if (browse_.page() == ViewModel::Browse::Page::Playlist) {
            const ActiveContext& context = browse_.context();
            sheet_->setDownloadAvailable(!context.isHistory && canDownloadPlaylist(context.sourceId, context.playlist));
        }
    });
    showDownloads();
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

void MainWindow::bindActivePlaylist()
{
    using ViewModel::ActivePlaylist;
    connect(&activePlaylist_, &ActivePlaylist::contextChanged, this, [this]() {
        refreshHero();
        syncSidebarSelection();
    });
    connect(&activePlaylist_, &ActivePlaylist::entriesChanged, this, &MainWindow::refreshMainList);
    connect(&activePlaylist_, &ActivePlaylist::loadingChanged, trackListBusyIndicator_, &QWidget::setVisible);
    connect(&activePlaylist_, &ActivePlaylist::startingRadioChanged, heroPanel_, &HeroPanel::setPlayBusy);
    // Playing something from the sheet (or the sidebar) brings the main
    // area back to front.

    // Whatever it has already — e.g. the playlist restored at startup.
    refreshMainList();
    refreshHero();
    trackListBusyIndicator_->setVisible(activePlaylist_.isLoading());
    heroPanel_->setPlayBusy(activePlaylist_.isStartingRadio());
    syncSidebarSelection();
}

void MainWindow::bindBrowse()
{
    using ViewModel::Browse;
    connect(&browse_, &Browse::pageChanged, this, &MainWindow::showBrowsePage);
    connect(&browse_, &Browse::rowsChanged, this, &MainWindow::showBrowseRows);
    connect(&browse_, &Browse::loadingChanged, this, [this](bool loading) {
        sheet_->setBusy(loading);
        showBrowseRows(); // the count in the header: the playlist's, then the list's
    });
    connect(&sourcePage_, &ViewModel::SourcePage::busyChanged, this, [this](const QString& sourceId, bool busy) {
        if (browse_.page() == Browse::Page::Source && browse_.sourceId() == sourceId)
            sourcePanel_->setAuthActionBusy(busy);
    });
    connect(&sourcePage_, &ViewModel::SourcePage::submitFailed, this,
        [this](const QString& sourceId, const QString& error) {
            if (browse_.page() == Browse::Page::Source && browse_.sourceId() == sourceId)
                sourcePanel_->showError(error);
        });
    // Whatever is open already — e.g. History, restored at startup.
    showBrowsePage();
}

void MainWindow::showBrowsePage()
{
    switch (browse_.page()) {
        case ViewModel::Browse::Page::None:
            if (sheet_->isPresented())
                sheet_->dismiss();
            break;
        case ViewModel::Browse::Page::Source:
            showSourcePage(browse_.sourceId());
            break;
        case ViewModel::Browse::Page::Playlist: {
            const ActiveContext& context = browse_.context();
            sheet_->trackModel()->clear();
            if (context.isRadio()) {
                sheet_->showRadio(radioPromo(context.playlist));
            } else if (context.isHistory) {
                sheet_->showTracks(tr("History"), QString(), QString(), QStringLiteral("history"), /*canPlayAll=*/true);
            } else {
                sheet_->showTracks(context.playlist.title, trackCountText(context.playlist.trackCount),
                    context.playlist.coverUrl.value_or(QString()), context.playlist.title, /*canPlayAll=*/true);
            }
            sheet_->setBusy(browse_.isLoading());
            sheet_->setDownloadAvailable(!context.isHistory && canDownloadPlaylist(context.sourceId, context.playlist));
            showBrowseRows();
            sheet_->present();
            break;
        }
    }
    syncSidebarSelection();
}

void MainWindow::showBrowseRows()
{
    if (browse_.page() != ViewModel::Browse::Page::Playlist || browse_.context().isRadio())
        return;
    QList<TrackListModel::MixedSourceEntry> rows;
    rows.reserve(browse_.rows().size());
    for (const ViewModel::Browse::Row& row : browse_.rows())
        rows.append({ row.sourceId, row.track, row.playedAt });
    sheet_->trackModel()->setMixedSourceTracks(rows);
    // Until the tracks are in, the count the playlist itself reports.
    sheet_->setSubtitle(trackCountText(browse_.isLoading() ? browse_.context().playlist.trackCount : int(rows.size())));
}

MainWindow::~MainWindow() { settings_.setWindowGeometry(saveGeometry()); }

void MainWindow::updateSourceAuthIndicator(const QString& sourceId)
{
    // The sidebar's icon is ViewModel::Sources' — just the page here.
    if (browse_.page() == ViewModel::Browse::Page::Source && browse_.sourceId() == sourceId)
        refreshAuthSection(sourceId, authStates_->state(sourceId));
}

void MainWindow::showSourcePage(const QString& sourceId)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    const QString sourceName = client != nullptr ? client->sourceName() : sourceId;
    const QString description = client != nullptr ? client->sourceDescription() : QString();
    const QJsonObject capabilities = client != nullptr ? client->capabilities() : QJsonObject();
    sheet_->showSource(sourceName, description, sources_.iconPath(sourceId));
    sourcePanel_->setSource(sourceId, capabilities);
    sourcePanel_->setPlaylists(sources_.playlists(sourceId), sources_.isLoading(sourceId));

    refreshAuthSection(sourceId, authStates_->state(sourceId));
    sourcePanel_->setAuthActionBusy(sourcePage_.isBusy(sourceId));
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

void MainWindow::onSourceChanged(const QString& sourceId)
{
    // Rows rebuilt: their selection went with them.
    syncSidebarSelection();
    if (browse_.page() == ViewModel::Browse::Page::Source && browse_.sourceId() == sourceId)
        sourcePanel_->setPlaylists(sources_.playlists(sourceId), sources_.isLoading(sourceId));
    // Which ones are favorites can change with the list (Playlist.featured).
    sourcePanel_->favoritesChanged();
}

void MainWindow::onSidebarActivated(const QModelIndex& index)
{
    const auto kind = static_cast<ViewModel::SidebarModel::Kind>(index.data(ViewModel::SidebarModel::KindRole).toInt());
    if (kind == ViewModel::SidebarModel::Kind::History) {
        browse_.openHistory();
        return;
    }
    if (kind == ViewModel::SidebarModel::Kind::SourceHeader) {
        // Unconditional — every source gets a panel (name/description/
        // capabilities), not just ones with an auth problem. See
        // SourcePanel's class doc.
        browse_.openSource(index.data(ViewModel::SidebarModel::SourceIdRole).toString());
        return;
    }
    if (kind != ViewModel::SidebarModel::Kind::Wave && kind != ViewModel::SidebarModel::Kind::Liked
        && kind != ViewModel::SidebarModel::Kind::Playlist)
        return;
    const QString sourceId = index.data(ViewModel::SidebarModel::SourceIdRole).toString();
    const Playlist playlist = index.data(ViewModel::SidebarModel::PlaylistDataRole).value<Playlist>();
    browse_.openPlaylist(sourceId, playlist);
}

void MainWindow::onSidebarDoubleClicked(const QModelIndex& index)
{
    const auto kind = static_cast<ViewModel::SidebarModel::Kind>(index.data(ViewModel::SidebarModel::KindRole).toInt());
    if (kind == ViewModel::SidebarModel::Kind::History) {
        activePlaylist_.activate(activePlaylist_.historyContext(), { }, 0); // activate() fills History's queue itself
        return;
    }
    if (kind != ViewModel::SidebarModel::Kind::Wave && kind != ViewModel::SidebarModel::Kind::Liked
        && kind != ViewModel::SidebarModel::Kind::Playlist)
        return;
    const QString sourceId = index.data(ViewModel::SidebarModel::SourceIdRole).toString();
    const Playlist playlist = index.data(ViewModel::SidebarModel::PlaylistDataRole).value<Playlist>();
    activePlaylist_.activateAndPlay(sourceId, playlist).detach();
}

void MainWindow::onSidebarContextMenuRequested(const QPoint& pos)
{
    const QModelIndex index = sidebarView_->indexAt(pos);
    if (!index.isValid())
        return;
    const auto kind = static_cast<ViewModel::SidebarModel::Kind>(index.data(ViewModel::SidebarModel::KindRole).toInt());
    const QString sourceId = index.data(ViewModel::SidebarModel::SourceIdRole).toString();

    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    if (kind == ViewModel::SidebarModel::Kind::SourceHeader) {
        menu->addAction(Theme::icon(QStringLiteral("refresh"), Theme::IconColor::Ink, 16),
            tr("Force Refresh Playlists"), this, [this, sourceId]() { sources_.refresh(sourceId); });
    } else if (kind == ViewModel::SidebarModel::Kind::Wave || kind == ViewModel::SidebarModel::Kind::Liked
        || kind == ViewModel::SidebarModel::Kind::Playlist) {
        menu->addAction(
            Theme::icon(QStringLiteral("play_arrow"), Theme::IconColor::Ink, 16), tr("Play"), this, [this, index]() {
                const QString sourceId = index.data(ViewModel::SidebarModel::SourceIdRole).toString();
                const Playlist playlist = index.data(ViewModel::SidebarModel::PlaylistDataRole).value<Playlist>();
                activePlaylist_.activateAndPlay(sourceId, playlist).detach();
            });
        const QString playlistId = index.data(ViewModel::SidebarModel::PlaylistIdRole).toString();
        const bool favorite = sources_.isFavorite(sourceId, playlistId);
        menu->addAction(
            Theme::icon(favorite ? QStringLiteral("star_border") : QStringLiteral("star"), Theme::IconColor::Ink, 16),
            favorite ? tr("Remove from Favorites") : tr("Add to Favorites"), this,
            [this, sourceId, playlistId]() { toggleFavorite(sourceId, playlistId); });
        const Playlist playlist = index.data(ViewModel::SidebarModel::PlaylistDataRole).value<Playlist>();
        if (canDownloadPlaylist(sourceId, playlist)) {
            menu->addAction(Theme::icon(QStringLiteral("file_download"), Theme::IconColor::Ink, 16),
                tr("Save Playlist to Downloads"), this,
                [this, sourceId, playlist]() { downloads_.downloadPlaylist(sourceId, playlist); });
        }
    } else {
        menu->deleteLater();
        return;
    }

    menu->popup(sidebarView_->viewport()->mapToGlobal(pos));
}

bool MainWindow::canDownloadPlaylist(const QString& sourceId, const Playlist& playlist) const
{
    if (!downloads_.isEnabled() || sourceId.isEmpty() || playlist.kind == QStringLiteral("radioStation"))
        return false;
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    return client != nullptr && client->capabilities().value(QStringLiteral("download")).toBool();
}

void MainWindow::onHeroContextMenuRequested(const QPoint& pos)
{
    // The active playlist's own menu — whatever the hero shows right now.
    const ActiveContext& active = activePlaylist_.context();
    if (!active.isValid())
        return;
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(Theme::icon(QStringLiteral("play_arrow"), Theme::IconColor::Ink, 16), tr("Play"), &activePlaylist_,
        &ViewModel::ActivePlaylist::play);
    if (!active.isHistory && canDownloadPlaylist(active.sourceId, active.playlist)) {
        menu->addAction(Theme::icon(QStringLiteral("file_download"), Theme::IconColor::Ink, 16),
            tr("Save Playlist to Downloads"), this,
            [this, active]() { downloads_.downloadPlaylist(active.sourceId, active.playlist); });
    }
    menu->popup(heroPanel_->mapToGlobal(pos));
}

void MainWindow::toggleFavorite(const QString& sourceId, const QString& playlistId)
{
    // A station (or Liked) taken out of the favorites leaves the sidebar
    // altogether — say once where it went, or it looks gone for good.
    const bool hidden = sources_.toggleFavorite(sourceId, playlistId);
    if (!hidden || settings_.hiddenFavoriteHintShown())
        return;
    const QList<Playlist> playlists = sources_.playlists(sourceId);
    const auto it
        = std::find_if(playlists.cbegin(), playlists.cend(), [&](const Playlist& p) { return p.id == playlistId; });
    if (it == playlists.cend())
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
        browse_.openSource(sourceId);
}

void MainWindow::refreshMainList()
{
    const ActiveContext& active = activePlaylist_.context();
    const bool hasActive = active.isValid() || playback_.hasQueue();
    emptyStatePlaceholder_->setVisible(!hasActive);
    contentSplitter_->setVisible(hasActive);

    const QVector<Playback::QueueEntry> entries = activePlaylist_.entries();
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
    const bool showList = !(rows.isEmpty() && active.isRadio());
    if (showList != trackListPane_->isVisibleTo(contentSplitter_))
        setTrackListVisible(showList);
}

void MainWindow::refreshHero()
{
    if (playback_.hasCurrentTrack())
        return; // trackChanged keeps the hero on the playing track
    const ActiveContext& active = activePlaylist_.context();
    if (!active.isValid()) {
        heroPanel_->clearNowPlaying();
        return;
    }
    heroPanel_->setPlaylist(active.isRadio() ? radioPromo(active.playlist) : active.playlist);
    heroPanel_->setPlayButtonVisible(true);
}

void MainWindow::syncSidebarSelection()
{
    // The sidebar selects what's on screen: the sheet's page while it's
    // open, the active playlist otherwise.
    const auto playlistIndex = [this](const ActiveContext& context) {
        return sidebarModel_->indexForPlaylist(context.isHistory ? QString() : context.sourceId,
            context.isHistory ? QStringLiteral("history") : context.playlist.id);
    };
    QModelIndex target;
    switch (browse_.page()) {
        case ViewModel::Browse::Page::Playlist:
            target = playlistIndex(browse_.context());
            break;
        case ViewModel::Browse::Page::Source:
            // Its header row. Not left as is — setSource() recreates that
            // row on every reload, and the view would otherwise leave the
            // selection on whatever row slid into its place.
            target = sidebarModel_->indexForSource(browse_.sourceId());
            break;
        case ViewModel::Browse::Page::None:
            if (activePlaylist_.context().isValid())
                target = playlistIndex(activePlaylist_.context());
            break;
    }
    if (target.isValid())
        sidebarView_->setCurrentIndex(target);
    else
        sidebarView_->clearSelection();
}

void MainWindow::restoreExpansion(const QModelIndex& parent, int first, int last)
{
    const std::function<void(const QModelIndex&)> apply = [&](const QModelIndex& index) {
        const QString key = ViewModel::SidebarModel::nodeKey(index);
        if (!key.isEmpty())
            sidebarView_->setExpanded(index, !sources_.isCollapsed(key));
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

void MainWindow::onTrackDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid())
        return;
    activePlaylist_.playRow(index.row());
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
    // Only while downloads are on (Settings → Downloads).
    const bool downloadSupported = downloads_.isEnabled() && capabilities.value(QStringLiteral("download")).toBool();
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
    if (playlistEditing_.canEdit(sourceId)) {
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
                activePlaylist_.startRadio(sourceId, id, context).detach();
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
                [this, sourceId, track]() { downloads_.downloadTrack(sourceId, track); });
        }
    }

    menu->popup(globalPos);
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
    QPointer<QMenu> menu, QString sourceId, Track track, std::optional<QPoint> reopenAt)
{
    const auto showNote = [&menu](const QString& text) {
        menu->clear();
        menu->addAction(text)->setEnabled(false);
    };
    showNote(tr("Loading…"));

    const std::optional<App::PlaylistEditing::Membership> membership
        = co_await playlistEditing_.membership(sourceId, track.id);
    if (!menu)
        co_return; // closed meanwhile
    if (!membership) {
        showNote(tr("Couldn't load playlists"));
        co_return;
    }
    if (membership->playlists.isEmpty()) {
        showNote(tr("No playlists"));
        co_return;
    }

    menu->clear();
    for (const Playlist& playlist : membership->playlists) {
        // A check box row rather than a checkable QAction: toggling one
        // doesn't close the menu, so several playlists can be changed in
        // one go — see MenuCheckRow.
        auto* row = new MenuCheckRow(playlist.title, menu);
        QCheckBox* box = row->checkBox();
        box->setChecked(membership->containing.contains(playlist.id));
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
    if (box)
        box->setEnabled(false); // one request at a time per playlist
    const bool done = co_await playlistEditing_.setTrackInPlaylist(sourceId, track, playlist, add);
    if (box) {
        if (!done) {
            const QSignalBlocker blocker(box);
            box->setChecked(!add); // back to how it really is
        }
        box->setEnabled(true);
    }
}

void MainWindow::showAboutDialog() { Ui::AboutDialog(this).exec(); }

void MainWindow::showSettingsDialog(const QString& openAt)
{
    SettingsDialog dialog(settings_, sourceManager_, *authStates_, downloads_, this, openAt);
    // While it's up, this window's toasts (an auth error from App::SourceSession,
    // a download finishing) go to the dialog instead: this window is
    // behind it, where they'd go unseen. Restored before the dialog, and
    // its notifier with it, is destroyed.
    ToastNotifier* const ownToasts = toastNotifier_;
    toastNotifier_ = dialog.toastNotifier();
    dialog.exec();
    toastNotifier_ = ownToasts;
    // A glass change from the General page, applied only now: switching
    // it replaces this window (Ui::WindowHost::recreate()), not something
    // to do under a modal dialog parented to it.
    Theme::setGlassEnabled(Theme::glassWanted(settings_.glassBackground())
        && Integration::WindowGlass::support() != Integration::WindowGlass::Support::None);
}

void MainWindow::quitForReal()
{
    reallyQuitting_ = true;
    close();
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
