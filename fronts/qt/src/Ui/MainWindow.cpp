#include "MainWindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QEvent>
#include <QListView>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "CoverArtCache.h"
#include "NowPlayingBar.h"
#include "PlaybackHistory.h"
#include "PlaylistHeader.h"
#include "RpcMethods.h"
#include "SettingsDialog.h"
#include "SidebarModel.h"
#include "SourcePanel.h"
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
    // Into NowPlayingBar's own transport-button row (top row, right end),
    // not a separate toolbar item — see setTrailingWidget()'s comment for
    // why the menu itself is still built here rather than in that class.
    auto* menuButton = new QToolButton(nowPlayingBar_);
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
    nowPlayingBar_->setTrailingWidget(menuButton);
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
    trackListLayout_ = new QVBoxLayout(trackListContainer);
    trackListLayout_->setContentsMargins(0, 0, 0, 0);
    trackListLayout_->setSpacing(0);
    // Stretch 0/1: without it, QVBoxLayout has no explicit weighting between
    // the two items in this direction, so it falls back to growing every
    // item proportionally to fill the container — stretching
    // playlistHeader_ with the window instead of leaving it at its
    // content-driven height. Giving trackListView_ all the stretch (and
    // playlistHeader_ none) is also why PlaylistHeader deliberately isn't
    // QSizePolicy::Fixed itself — see that class's constructor for why that
    // specific combination (Fixed + a width-dependent heightForWidth)
    // fights window resizing instead. setTrackListVisible() swaps which of
    // the two gets the stretch when trackListView_ is hidden entirely
    // (radioStation/My Wave) — see its own comment in the header.
    trackListLayout_->addWidget(playlistHeader_);
    trackListLayout_->addWidget(trackListView_, 1);

    sourcePanel_ = new SourcePanel(coverArtCache_, trackListContainer);
    connect(sourcePanel_, &SourcePanel::submitRequested, this,
            [this](const QString& sourceId, const QJsonObject& fields) { submitAuthAsync(sourceId, fields).detach(); });
    connect(sourcePanel_, &SourcePanel::retryRequested, this,
            [this](const QString& sourceId) { retryAuthAsync(sourceId).detach(); });
    trackListLayout_->addWidget(sourcePanel_, 1);

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
    playlistHeader_->hide();
    trackListView_->hide();
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
            qCWarning(lcMainWindow) << "catalog.listPlaylists failed for" << client->sourceId() << ":"
                                     << e.what();
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
    showingHistory_ = true;
    currentPlaylistSourceId_.clear(); // no single source — the header's Play-all button is hidden below anyway
    currentPlaylist_ = Playlist { QStringLiteral("history"), tr("History"), std::nullopt, std::nullopt,
                                  static_cast<int>(playbackHistory_->entries().size()), QStringLiteral("playlist") };
    // Before setPlaylist(), not after — see setTrackListVisible()'s comment.
    setTrackListVisible(true);
    playlistHeader_->setPlaylist(currentPlaylist_);
    playlistHeader_->setPlayButtonVisible(false);

    QList<TrackListModel::MixedSourceEntry> entries;
    entries.reserve(playbackHistory_->entries().size());
    for (const History::HistoryEntry& e : playbackHistory_->entries())
        entries.append({ e.sourceId, e.track, e.playedAt });
    trackListModel_->setMixedSourceTracks(entries);

    // Deferred to the next event-loop iteration, not called synchronously
    // here — playlistHeader_->setPlaylist() above just posted a
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
    showingHistory_ = false;
    playlistHeader_->setPlayButtonVisible(true);
    currentPlaylistSourceId_ = sourceId;
    currentPlaylist_ = playlist;

    // Before setPlaylist(), not after — see setTrackListVisible()'s comment:
    // it decides how large a generated cover to render from
    // playlistHeader_'s *current* size(), which needs to already reflect
    // this stretch change.
    const bool isRadioStation = playlist.kind == QStringLiteral("radioStation");
    setTrackListVisible(!isRadioStation);
    playlistHeader_->setPlaylist(playlist);

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
    playlistHeader_->setPlayBusy(true);
    try {
        StartRadioParams params { seed };
        StartRadioResult result = co_await Rpc::catalogStartRadio(*client, params);
        playback_.startRadio(sourceId, result.stationId, result.initialTracks);
    } catch (const std::exception& e) {
        qCWarning(lcMainWindow) << "starting radio failed for" << sourceId << ":" << e.what();
        toastNotifier_->showError(QString::fromStdString(e.what()));
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

void MainWindow::setTrackListVisible(bool visible)
{
    trackListView_->setVisible(visible);
    trackListLayout_->setStretchFactor(playlistHeader_, visible ? 0 : 1);
    // Force the new geometry through synchronously instead of leaving it
    // for the next event-loop pass: every caller calls this before
    // PlaylistHeader::setPlaylist(), which reads playlistHeader_->size() to
    // decide how large a generated cover to render (see
    // GeneratedCoverArt.h) — without this, that size() call still sees
    // whatever this widget's size was under its *previous* stretch factor,
    // and setScaledContents then stretches the resulting cover up to the
    // real (larger) banner, visibly blurry/banded.
    trackListLayout_->activate();
}

} // namespace Ui
