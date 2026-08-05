#include "MainWindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QEvent>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "AuthBanner.h"
#include "CoverArtCache.h"
#include "NowPlayingBar.h"
#include "PlaybackHistory.h"
#include "PlaylistHeader.h"
#include "RpcMethods.h"
#include "SettingsDialog.h"
#include "SidebarModel.h"
#include "ToastNotifier.h"
#include "TrackListModel.h"
#include "TrackRowDelegate.h"

namespace Ui {

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

    coverArtCache_ = new CoverArtCache(this);
    playbackHistory_ = new History::PlaybackHistory(this);

    // --- now-playing controls, merged into the top toolbar alongside the
    // hamburger menu (see AGENTS.md/the plan: standard system frame, so
    // this sits below the OS titlebar, not literally overlapping its
    // buttons) ---
    nowPlayingBar_ = new NowPlayingBar(coverArtCache_, this);
    nowPlayingBar_->setVolume(settings_.volume());
    connect(nowPlayingBar_, &NowPlayingBar::playPauseClicked, &playback_, &Playback::PlaybackController::togglePause);
    connect(nowPlayingBar_, &NowPlayingBar::nextClicked, &playback_, &Playback::PlaybackController::next);
    connect(nowPlayingBar_, &NowPlayingBar::previousClicked, &playback_, &Playback::PlaybackController::previous);
    connect(nowPlayingBar_, &NowPlayingBar::stopClicked, &playback_, &Playback::PlaybackController::stop);
    connect(nowPlayingBar_, &NowPlayingBar::seekRequested, &playback_, &Playback::PlaybackController::seek);
    connect(nowPlayingBar_, &NowPlayingBar::volumeChanged, this, [this](int v) {
        playback_.setVolume(v);
        settings_.setVolume(v);
    });

    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
            [this](const Track& track, const QString&) { nowPlayingBar_->setTrack(track); });
    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
            [this](const Track& track, const QString& sourceId) { playbackHistory_->record(sourceId, track); });
    connect(playbackHistory_, &History::PlaybackHistory::changed, this, [this]() {
        if (showingHistory_)
            showHistory(); // refresh in place — a track just started playing
    });
    connect(&playback_, &Playback::PlaybackController::playingChanged, nowPlayingBar_, &NowPlayingBar::setPlaying);
    connect(&playback_, &Playback::PlaybackController::loadingChanged, nowPlayingBar_, &NowPlayingBar::setLoading);
    connect(&playback_, &Playback::PlaybackController::positionChanged, nowPlayingBar_, &NowPlayingBar::setPosition);

    auto* toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->addWidget(nowPlayingBar_);
    auto* menuButton = new QToolButton(toolbar);
    menuButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
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
    toolbar->addWidget(menuButton);
    addToolBar(toolbar);

    // --- sidebar + track list ---
    sidebarModel_ = new SidebarModel(this);
    sidebarModel_->ensureHistoryItem();
    sidebarView_ = new QTreeView(this);
    sidebarView_->setModel(sidebarModel_);
    sidebarView_->setHeaderHidden(true);
    // Items are QStandardItems, editable by default — without this, the
    // double-click wired below to start playback also opens a rename
    // editor on the row.
    sidebarView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Left at the style's native branch rendering (arrows + connector
    // lines) — tried stripping just the lines while keeping arrows via a
    // QSS ::branch override, but the style draws them as one primitive per
    // state, so trimming one always distorted or dropped the other.
    sidebarView_->setIndentation(12);
    connect(sidebarModel_, &QStandardItemModel::rowsInserted, sidebarView_, &QTreeView::expandAll);
    connect(sidebarView_, &QTreeView::clicked, this, &MainWindow::onSidebarActivated);
    connect(sidebarView_, &QTreeView::doubleClicked, this, &MainWindow::onSidebarDoubleClicked);

    playlistHeader_ = new PlaylistHeader(coverArtCache_, this);
    connect(playlistHeader_, &PlaylistHeader::playClicked, this, &MainWindow::playCurrentPlaylist);

    trackListModel_ = new TrackListModel(this);
    trackRowDelegate_ = new TrackRowDelegate(coverArtCache_, this);
    connect(coverArtCache_, &CoverArtCache::pixmapReady, this, [this]() { trackListView_->viewport()->update(); });
    trackListView_ = new QListView(this);
    trackListView_->setModel(trackListModel_);
    trackListView_->setItemDelegate(trackRowDelegate_);
    // Needed for State_MouseOver to be set at all — see the delegate's
    // hover-only play button drawn over the cover thumbnail.
    trackListView_->setMouseTracking(true);
    connect(trackListView_, &QListView::doubleClicked, this, &MainWindow::onTrackDoubleClicked);
    connect(trackRowDelegate_, &TrackRowDelegate::playRequested, this, &MainWindow::onTrackDoubleClicked);

    auto* trackListContainer = new QWidget(this);
    auto* trackListLayout = new QVBoxLayout(trackListContainer);
    trackListLayout->setContentsMargins(0, 0, 0, 0);
    trackListLayout->setSpacing(0);
    trackListLayout->addWidget(playlistHeader_);
    // Stretch 1: without it, QVBoxLayout has no explicit weighting between
    // the two items in this direction, so it falls back to growing every
    // item proportionally to fill the container — stretching
    // playlistHeader_ with the window instead of leaving it at its
    // content-driven height. Giving trackListView_ all the stretch (and
    // playlistHeader_ none) is also why PlaylistHeader deliberately isn't
    // QSizePolicy::Fixed itself — see that class's constructor for why that
    // specific combination (Fixed + a width-dependent heightForWidth)
    // fights window resizing instead.
    trackListLayout->addWidget(trackListView_, 1);

    // Not part of trackListLayout: a layout-managed progress bar would
    // shrink the list by its own height whenever it's shown/hidden,
    // shoving the whole view down. It's a free-floating child positioned
    // absolutely over trackListView_'s top edge instead (not the
    // container's — playlistHeader_ above it can be shown/hidden too,
    // which shifts where the list itself actually starts) — see
    // eventFilter()/repositionTrackListBusyIndicator().
    trackListBusyIndicator_ = new QProgressBar(trackListContainer);
    trackListBusyIndicator_->setRange(0, 0);
    trackListBusyIndicator_->setMaximumHeight(4);
    trackListBusyIndicator_->setTextVisible(false);
    trackListBusyIndicator_->hide();
    trackListContainer->installEventFilter(this);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(sidebarView_);
    splitter->addWidget(trackListContainer);
    splitter->setSizes({ settings_.sidebarWidth(), 720 });
    connect(splitter, &QSplitter::splitterMoved, this,
            [this, splitter]() { settings_.setSidebarWidth(splitter->sizes().first()); });

    authBanner_ = new AuthBanner(this);
    connect(authBanner_, &AuthBanner::submitRequested, this,
            [this](const QString& sourceId, const QJsonObject& fields) { submitAuthAsync(sourceId, fields).detach(); });

    toastNotifier_ = new ToastNotifier(this);
    connect(&playback_, &Playback::PlaybackController::errorOccurred, this,
            [this](const QString& message) { toastNotifier_->showError(message); });

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(authBanner_);
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
    client->onAuthPromptRaw
        = [this, client](const QJsonObject& params) { authBanner_->showPrompt(client->sourceId(), params); };
    client->notifications.onAuthStatusChanged = [this, client](const StatusChangedParams& status) {
        if (status.status == QStringLiteral("authenticated")) {
            authBanner_->showAuthenticated(client->sourceId());
            loadPlaylistsAsync(client).detach();
        } else {
            authBanner_->showError(client->sourceId(), status.message.value_or(QString()));
        }
    };

    const QJsonObject auth = client->capabilities().value(QStringLiteral("auth")).toObject();
    if (auth.value(QStringLiteral("required")).toBool()) {
        Rpc::authGetStatus(*client).detach(); // fire-and-forget: auth/statusChanged or a prior session drives the UI
    }
    loadPlaylistsAsync(client).detach();
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
        } catch (const Rpc::RpcCallException&) {
            // Not authenticated yet, or a transient failure — the sidebar
            // simply won't show playlists for this source until it retries
            // (e.g. after auth completes, see wireSource's onAuthStatusChanged).
        }
    }
    sidebarModel_->setSource(client->sourceId(), client->sourceName(), playlists);
}

void MainWindow::onSidebarActivated(const QModelIndex& index)
{
    const auto kind = static_cast<SidebarModel::Kind>(index.data(SidebarModel::KindRole).toInt());
    if (kind == SidebarModel::Kind::History) {
        showHistory();
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
    showingHistory_ = true;
    currentPlaylistSourceId_.clear(); // no single source — the header's Play-all button is hidden below anyway
    currentPlaylist_ = Playlist { QStringLiteral("history"), tr("History"), std::nullopt, std::nullopt,
                                  static_cast<int>(playbackHistory_->entries().size()), QStringLiteral("playlist") };
    playlistHeader_->setPlaylist(currentPlaylist_);
    playlistHeader_->setPlayButtonVisible(false);

    QList<TrackListModel::MixedSourceEntry> entries;
    entries.reserve(playbackHistory_->entries().size());
    for (const History::HistoryEntry& e : playbackHistory_->entries())
        entries.append({ e.sourceId, e.track, e.playedAt });
    trackListModel_->setMixedSourceTracks(entries);

    trackListView_->show();
    repositionTrackListBusyIndicator();
    trackListBusyIndicator_->hide();
}

Rpc::Task<void> MainWindow::showPlaylistAsync(QString sourceId, Playlist playlist)
{
    showingHistory_ = false;
    playlistHeader_->setPlayButtonVisible(true);
    currentPlaylistSourceId_ = sourceId;
    currentPlaylist_ = playlist;
    playlistHeader_->setPlaylist(playlist);

    if (playlist.kind == QStringLiteral("radioStation")) {
        // Continuous, not a fixed list — see docs/protocol.md's
        // Playlist.kind note. Only the header + Play button show; no RPC
        // call here, that's what makes this not auto-play (the Play button
        // handler wired in the constructor calls startRadioAsync()).
        trackListView_->hide();
        trackListModel_->clear();
        co_return;
    }

    trackListView_->show();
    repositionTrackListBusyIndicator();
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
    } catch (const Rpc::RpcCallException& e) {
        toastNotifier_->showError(e.error().message);
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
    playlistHeader_->setPlayBusy(true);
    try {
        StartRadioParams params { seed };
        StartRadioResult result = co_await Rpc::catalogStartRadio(*client, params);
        playback_.startRadio(sourceId, result.stationId, result.initialTracks);
    } catch (const Rpc::RpcCallException& e) {
        toastNotifier_->showError(e.error().message);
    }
    playlistHeader_->setPlayBusy(false);
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

Rpc::Task<void> MainWindow::submitAuthAsync(QString sourceId, QJsonObject fields)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    authBanner_->setSubmitBusy(true);
    try {
        SubmitParams params;
        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
            params.fields.insert(it.key(), it.value().toString());
        }
        co_await Rpc::authSubmit(*client, params);
    } catch (const Rpc::RpcCallException& e) {
        authBanner_->showError(sourceId, e.error().message);
    }
    authBanner_->setSubmitBusy(false);
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(this, tr("About CloudMus"),
                       tr("CloudMus — a lightweight Qt frontend for cloudmus music sources."));
}

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
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::repositionTrackListBusyIndicator()
{
    trackListBusyIndicator_->setGeometry(trackListView_->x(), trackListView_->y(), trackListView_->width(), 4);
}

} // namespace Ui
