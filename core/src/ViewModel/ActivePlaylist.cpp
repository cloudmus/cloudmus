#include "ActivePlaylist.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include "CoverArtCache.h"
#include "Messages.h"
#include "PlaybackHistory.h"
#include "PlaylistEditing.h"
#include "Preloader.h"
#include "ProtocolParseError.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "SourceManager.h"
#include "Sources.h"
#include "TrackFetch.h"
#include "TrackStates.h"

namespace ViewModel {

namespace {
Q_LOGGING_CATEGORY(lcActivePlaylist, "cloudmus.viewmodel.activeplaylist")

// What a process killed without notice loses of where it was.
constexpr int kPositionSaveIntervalMs = 3000;

QString cachedTracksPath(const Config::Settings& settings)
{
    return QFileInfo(settings.filePath()).absolutePath() + QStringLiteral("/active-playlist.json");
}
}

ActivePlaylist::ActivePlaylist(Playback::PlaybackController& playback, Rpc::SourceManager& sourceManager,
    Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, History::PlaybackHistory& history,
    Config::Settings& settings, Sources& sources, App::PlaylistEditing& playlistEditing, Messages& messages,
    const App::PlaylistTrackCache& playlistTracks, QObject* parent)
    : QObject(parent)
    , playback_(playback)
    , sourceManager_(sourceManager)
    , trackStates_(trackStates)
    , coverArtCache_(coverArtCache)
    , history_(history)
    , settings_(settings)
    , sources_(sources)
    , messages_(messages)
    , playlistTracks_(playlistTracks)
    , positionSave_(kPositionSaveIntervalMs, [this]() { savePosition(); })
{
    connect(&playback_, &Playback::PlaybackController::positionChanged, &positionSave_, &Timing::Throttle::trigger);
    connect(&playback_, &Playback::PlaybackController::playingChanged, this, [this](bool playing) {
        if (!playing)
            savePosition();
    });
    connect(this, &ActivePlaylist::contextChanged, this, &ActivePlaylist::titleChanged);
    connect(this, &ActivePlaylist::entriesChanged, this, &ActivePlaylist::countChanged);
    connect(&playback_, &Playback::PlaybackController::queueChanged, this, [this]() {
        // A radio's track list grows through notifications, outside loadTracks().
        const bool hadInputs = !restoredInputs_.isEmpty();
        restoredInputs_.clear();
        int position = 0;
        for (const auto& entry : playback_.queue()) {
            if (!entry.localInput.isEmpty()) {
                if (entry.persistentInput)
                    restoredInputs_.append({ position, entry });
            } else {
                ++position;
            }
        }
        if (hadInputs || !restoredInputs_.isEmpty() || (context_.isRadio() && playback_.isRadio()))
            saveCachedTracks();
        emit entriesChanged();
    });
    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString& sourceId) {
            if (!context_.isValid())
                return;
            const auto& entry = playback_.queue()[playback_.currentIndex()];
            if (!entry.localInput.isEmpty()) {
                if (entry.persistentInput) {
                    lastLocalInputId_ = track.id;
                    settings_.setLastActiveTrack(track.id, playback_.currentIndex());
                    saveCachedTracks();
                }
                return;
            }
            if (!lastLocalInputId_.isEmpty()) {
                lastLocalInputId_.clear();
                saveCachedTracks();
            }
            if (!context_.isHistory && sourceId != context_.sourceId)
                return;
            if (context_.isRadio() && entry.userQueued)
                return;
            settings_.setLastActiveTrack(track.id, playback_.currentIndex());
        });
    connect(&playlistEditing, &App::PlaylistEditing::playlistEdited, this, &ActivePlaylist::applyPlaylistEdit);
    // What resumePlayback() goes by next run: on whenever a track starts,
    // off only when the user pauses or stops — not when quitting stops it.
    connect(&playback_, &Playback::PlaybackController::playingChanged, this, [this](bool playing) {
        if (playing)
            settings_.setWasPlaying(true);
    });
    connect(&playback_, &Playback::PlaybackController::halted, this, [this]() { settings_.setWasPlaying(false); });

    // Restore the last active playlist (without playing it). History is
    // local, so it's restored right away; a source's playlist has to wait
    // for that source to list its playlists.
    const Config::Settings::ActivePlaylistRef saved = settings_.lastActivePlaylist();
    loadCachedTracks();
    connect(&trackStates_, &Library::TrackStates::feedbackChanged, this,
        [this](const QString& sourceId, const QString& trackId) {
            ++feedbackRevision_;
            if (!context_.isValid())
                return;
            for (const Playback::QueueEntry& entry : entries()) {
                if (entry.sourceId == sourceId && entry.track.id == trackId) {
                    saveCachedTracks();
                    break;
                }
            }
        });
    connect(&sources_, &Sources::playlistsLoaded, this, [this](const QString& sourceId, const QList<Playlist>&) {
        // What played before its source was up gets going: downloads
        // behind it, and a radio's station.
        playback_.sourceCameUp(sourceId);
        // What failed to load for lack of a network, now that there is one.
        if (tracksStale_ && !context_.isHistory && context_.sourceId == sourceId && pendingRestore_.playlistId.isEmpty()
            && !sources_.listingFailed(sourceId))
            loadTracks(context_).detach();
        if (context_.isRadio() && context_.sourceId == sourceId && playback_.awaitsStation())
            attachStation(sourceId, context_.playlist.id).detach();
        // Asked to play with nothing saved of the radio and its source away.
        if (context_.isRadio() && context_.sourceId == sourceId && pendingRestore_.playlistId.isEmpty())
            resumePlayback();
        if (context_.isRadio() && context_.sourceId == sourceId && !tracks_.isEmpty()) {
            const QString playlistId = context_.playlist.id;
            QTimer::singleShot(refreshDelayMs_, this,
                [this, sourceId, playlistId]() { refreshRadioLikes(sourceId, playlistId).detach(); });
        }
    });
    if (saved.kind == QStringLiteral("history")) {
        if (!context_.isValid())
            setContext(historyContext());
        loadTracks(context_).detach();
    } else if (!saved.sourceId.isEmpty() && !saved.playlistId.isEmpty()) {
        pendingRestore_ = saved;
        connect(&sources_, &Sources::playlistsLoaded, this, &ActivePlaylist::restoreFrom);
    }

    resumePending_ = settings_.resumePlaybackAtStartup() && settings_.wasPlaying()
        && (context_.isValid() || !pendingRestore_.playlistId.isEmpty());
    if (resumePending_) {
        // The saved song plays from disk, if it is there. Later, so that
        // whoever listens to playback is connected by then.
        QMetaObject::invokeMethod(this, &ActivePlaylist::resumeFromCache, Qt::QueuedConnection);
    }
    // History is back already, but a saved song that is not on disk can only
    // play once the source it came from is up.
    connect(&sources_, &Sources::playlistsLoaded, this, [this](const QString& sourceId, const QList<Playlist>&) {
        if (resumePending_ && context_.isHistory && !tracks_.isEmpty() && tracks_[resumeIndex()].sourceId == sourceId)
            resumePlayback();
    });
}

void ActivePlaylist::resume()
{
    if (playback_.hasCurrentTrack() || playback_.isStarting())
        return;
    // Stopped, or the track couldn't be started: the queue is still there.
    if (playback_.hasQueue()) {
        resumePosition();
        playback_.playAt(qMax(0, savedRow()));
        return;
    }
    if (!context_.isValid() && pendingRestore_.playlistId.isEmpty())
        return;
    resumePending_ = true;
    resumeFromCache();
    if (!resumePending_ || !context_.isValid())
        return; // playing already, or restoreFrom() takes it from here
    // What was saved plays without waiting for the source to list anything:
    // the player itself waits for the source, and shows that it does.
    if (!tracks_.isEmpty()) {
        if (!context_.isRadio()) {
            resumePending_ = false;
            resumePosition();
            int index = resumeIndex();
            const auto queue = restoredQueue(index);
            playback_.loadQueue(queue, index);
        } else if (offline_ && offline_(context_.sourceId)) {
            resumePlayback();
        } else {
            resumePending_ = false;
            resumePosition();
            playSavedRadio();
            // Else it comes once the source is up (see the constructor).
            if (sourceAnswers(context_.sourceId))
                attachStation(context_.sourceId, context_.playlist.id).detach();
        }
        return;
    }
    if (!pendingRestore_.playlistId.isEmpty() || context_.isHistory)
        return;
    if (context_.isRadio()) {
        if (sourceAnswers(context_.sourceId) || !sourceManager_.isStarting(context_.sourceId))
            resumePlayback();
    } else if (sourceAnswers(context_.sourceId)) {
        loadTracks(context_).detach();
    }
}

void ActivePlaylist::step(int delta)
{
    if (playback_.hasCurrentTrack() || playback_.isStarting())
        return;
    const int row = savedRow() + delta;
    if (savedRow() < 0 || row < 0 || row >= count()) {
        resume();
        return;
    }
    playRow(row);
}

void ActivePlaylist::loadCachedTracks()
{
    QFile file(cachedTracksPath(settings_));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return;
    const QJsonObject data = document.object();
    const Config::Settings::ActivePlaylistRef saved = settings_.lastActivePlaylist();
    if (data.value(QStringLiteral("sourceId")).toString() != saved.sourceId
        || data.value(QStringLiteral("playlistId")).toString() != saved.playlistId
        || data.value(QStringLiteral("kind")).toString() != saved.kind)
        return;
    try {
        const QJsonValue playlist = data.value(QStringLiteral("playlist"));
        const QJsonValue rows = data.value(QStringLiteral("tracks"));
        if (!playlist.isObject() || !rows.isArray())
            return;
        const Playlist cachedPlaylist = Playlist::fromJson(playlist.toObject());
        if (cachedPlaylist.id != saved.playlistId)
            return;
        context_ = { saved.sourceId, cachedPlaylist, saved.kind == QStringLiteral("history") };
        for (const QJsonValue& row : rows.toArray()) {
            if (!row.isObject())
                continue;
            const QJsonObject entry = row.toObject();
            if (entry.value(QStringLiteral("track")).isObject()) {
                try {
                    const QString sourceId = entry.value(QStringLiteral("sourceId")).toString();
                    const Track track = Track::fromJson(entry.value(QStringLiteral("track")).toObject());
                    tracks_.append({ sourceId, track });
                    trackStates_.observe(sourceId, track, /*onlyIfUnknown=*/true);
                } catch (const Rpc::ProtocolParseError&) {
                    continue;
                }
            }
        }
        lastLocalInputId_ = data.value(QStringLiteral("lastLocalInput")).toString();
        for (const QJsonValue& value : data.value(QStringLiteral("localInputs")).toArray()) {
            const QJsonObject row = value.toObject();
            const QString uri = row.value(QStringLiteral("url")).toString();
            const QUrl url(uri);
            if (!Playback::AudioPlayer::isLocalInput(url))
                continue;
            try {
                const Track track = Track::fromJson(row.value(QStringLiteral("track")).toObject());
                restoredInputs_.append({ row.value(QStringLiteral("position")).toInt(),
                    { QStringLiteral("local-file"), track, true, uri, true } });
            } catch (const Rpc::ProtocolParseError&) {
                continue;
            }
        }
    } catch (const Rpc::ProtocolParseError&) {
        context_ = { };
        tracks_.clear();
        restoredInputs_.clear();
        lastLocalInputId_.clear();
    }
}

void ActivePlaylist::saveCachedTracks() const
{
    if (!context_.isValid())
        return;
    QJsonArray rows;
    const QVector<Playback::QueueEntry>& entries
        = context_.isRadio() && playback_.isRadio() ? playback_.queue() : tracks_;
    for (const Playback::QueueEntry& entry : entries) {
        if (context_.isRadio() && (entry.userQueued || entry.sourceId != context_.sourceId))
            continue;
        Track track = entry.track;
        const Library::TrackState state = trackStates_.state(entry.sourceId, track.id);
        if (state.liked)
            track.liked = state.liked;
        if (state.disliked)
            track.disliked = state.disliked;
        rows.append(QJsonObject {
            { QStringLiteral("sourceId"), entry.sourceId }, { QStringLiteral("track"), track.toJson() } });
    }
    QJsonArray inputs;
    for (const auto& [position, entry] : restoredInputs_)
        inputs.append(QJsonObject { { QStringLiteral("position"), position },
            { QStringLiteral("url"), entry.localInput }, { QStringLiteral("track"), entry.track.toJson() } });
    const QJsonObject data { { QStringLiteral("sourceId"), context_.sourceId },
        { QStringLiteral("playlistId"), context_.playlist.id },
        { QStringLiteral("kind"), context_.isHistory ? QStringLiteral("history") : context_.playlist.kind },
        { QStringLiteral("playlist"), context_.playlist.toJson() }, { QStringLiteral("tracks"), rows },
        { QStringLiteral("localInputs"), inputs }, { QStringLiteral("lastLocalInput"), lastLocalInputId_ } };
    const QString path = cachedTracksPath(settings_);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file.write(QJsonDocument(data).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

PlaylistContext ActivePlaylist::historyContext() const
{
    PlaylistContext context;
    context.isHistory = true;
    context.playlist = Playlist { QStringLiteral("history"), tr("History"), std::nullopt, std::nullopt,
        static_cast<int>(history_.entries().size()), QStringLiteral("playlist") };
    return context;
}

QVector<Playback::QueueEntry> ActivePlaylist::entries() const
{
    if (playback_.hasQueue())
        return playback_.queue();
    if (restoredInputs_.isEmpty())
        return tracks_;
    int unused = 0;
    return restoredQueue(unused);
}

int ActivePlaylist::resumeIndex() const
{
    const QString savedId = settings_.lastActiveTrackId();
    const int savedIndex = settings_.lastActiveTrackIndex();
    if (savedId.isEmpty() || tracks_.isEmpty())
        return 0;
    if (savedIndex >= 0 && savedIndex < tracks_.size() && tracks_[savedIndex].track.id == savedId)
        return savedIndex;
    for (int i = 0; i < tracks_.size(); ++i) {
        if (tracks_[i].track.id == savedId)
            return i;
    }
    return 0;
}

int ActivePlaylist::savedRow() const
{
    const QString savedId = settings_.lastActiveTrackId();
    if (savedId.isEmpty())
        return -1;
    const QVector<Playback::QueueEntry> rows = entries();
    const int savedIndex = settings_.lastActiveTrackIndex();
    if (savedIndex >= 0 && savedIndex < rows.size() && rows[savedIndex].track.id == savedId)
        return savedIndex;
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i].track.id == savedId)
            return i;
    }
    return -1;
}

std::optional<Playback::QueueEntry> ActivePlaylist::savedEntry() const
{
    const int row = savedRow();
    if (row < 0)
        return std::nullopt;
    return entries()[row];
}

void ActivePlaylist::setContext(const PlaylistContext& context)
{
    const bool changed = !context_.sameAs(context);
    context_ = context;
    if (changed) {
        tracks_.clear();
        restoredInputs_.clear();
        lastLocalInputId_.clear();
    }
    sources_.model().setActivePlaylist(context.isHistory ? QString() : context.sourceId,
        context.isHistory ? QStringLiteral("history") : context.playlist.id);
    settings_.setLastActivePlaylist({ context.isHistory ? QString() : context.sourceId, context.playlist.id,
        context.isHistory ? QStringLiteral("history") : context.playlist.kind });
    // Something the user made active wins over what's still to restore.
    pendingRestore_ = Config::Settings::ActivePlaylistRef();
    if (changed)
        resumePending_ = false;
    emit contextChanged();
    emit entriesChanged();
}

void ActivePlaylist::activate(
    const PlaylistContext& context, const QVector<Playback::QueueEntry>& entries, int startIndex)
{
    if (context.isRadio()) {
        startRadio(context.sourceId, context.playlist.id, context).detach();
        emit activated();
        return;
    }
    QVector<Playback::QueueEntry> queue = entries;
    if (queue.isEmpty() && context.isHistory) {
        for (const History::HistoryEntry& e : history_.entries())
            queue.append(Playback::QueueEntry { e.sourceId, e.track });
    }
    if (queue.isEmpty())
        return;
    setContext(context);
    tracks_ = queue;
    saveCachedTracks();
    playback_.loadQueue(queue, qBound(0, startIndex, int(queue.size()) - 1));
    emit activated();
}

Rpc::Task<void> ActivePlaylist::activateAndPlay(QString sourceId, Playlist playlist)
{
    const PlaylistContext context { sourceId, playlist };
    if (context.isRadio()) {
        activate(context, { }, 0);
        co_return;
    }
    // Its tracks as they were last listed play at once when the source
    // can't answer (still starting, or no network): the downloaded ones
    // sound, and the source catches up behind them (see tracksStale_).
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr || !client->available()) {
        if (activateFromCache(context))
            co_return;
    }
    try {
        const QVector<Playback::QueueEntry> entries = co_await App::fetchTracks(
            sourceManager_, trackStates_, coverArtCache_, sourceId, playlist, &playlistTracks_);
        activate(context, entries, 0);
    } catch (const std::exception& e) {
        qCWarning(lcActivePlaylist) << "loading tracks failed for" << sourceId << ":" << e.what();
        if (activateFromCache(context))
            co_return;
        messages_.failure(QString(), e, /*quietOffline=*/true);
    }
}

bool ActivePlaylist::activateFromCache(const PlaylistContext& context)
{
    const std::optional<QVector<Playback::QueueEntry>> cached
        = App::cachedTracks(playlistTracks_, trackStates_, coverArtCache_, context.sourceId, context.playlist);
    if (!cached)
        return false;
    qCInfo(lcActivePlaylist) << "playing" << context.playlist.id << "from its saved tracks";
    activate(context, *cached, 0);
    tracksStale_ = true; // refreshed once its source lists its playlists
    return true;
}

StartRadioParams ActivePlaylist::radioParams(const QString& sourceId, const QString& seed) const
{
    StartRadioParams params { seed };
    // A station knows what it has served since it was started, no more:
    // what played before that is in History.
    const int days = settings_.sourceRadioRepeatDays(sourceId);
    const QList<Track> recent = history_.recentTracks(sourceId, settings_.sourceRadioRepeatGap(sourceId),
        days > 0 ? QDateTime::currentDateTimeUtc().addDays(-days) : QDateTime());
    if (recent.isEmpty())
        return params;
    // The ids for a source before 1.18; the titles and artists let a newer
    // one hold back other versions of these songs too.
    QStringList ids;
    QList<RecentTrack> tracks;
    for (const Track& track : recent) {
        ids.append(track.id);
        QStringList artists;
        for (const Artist& artist : track.artists)
            artists.append(artist.name);
        tracks.append(RecentTrack { track.id, track.title, artists });
    }
    params.recentTrackIds = ids;
    params.recentTracks = tracks;
    return params;
}

Rpc::Task<void> ActivePlaylist::startRadio(
    QString sourceId, QString seed, PlaylistContext context, std::optional<Track> resumeTrack)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    // Known to be offline: nothing to ask — and the asking can take longer
    // than the call may (a name lookup with no network), to fail as a
    // timeout rather than as "offline".
    if (offline_ && offline_(sourceId)) {
        startOfflineRadio(context, client->sourceName());
        co_return;
    }
    startingRadio_ = true;
    emit startingRadioChanged(true);
    try {
        StartRadioResult result = co_await Rpc::catalogStartRadio(*client, radioParams(sourceId, seed));
        QList<Track> initialTracks = result.initialTracks;
        if (resumeTrack) {
            // The old station session cannot be resumed, but its last song
            // can lead the new session before its freshly generated tracks.
            initialTracks.removeIf([&](const Track& track) { return track.id == resumeTrack->id; });
        }
        trackStates_.observe(sourceId, initialTracks);
        if (resumeTrack)
            trackStates_.observe(sourceId, *resumeTrack, /*onlyIfUnknown=*/true);
        if (resumeTrack)
            initialTracks.prepend(*resumeTrack);
        coverArtCache_.assignSource(sourceId, initialTracks);
        // Before startRadio(), so the queue it emits lands in the main
        // list under the right playlist.
        setContext(context);
        playback_.startRadio(sourceId, result.stationId, initialTracks);
    } catch (const Rpc::RpcCallException& e) {
        qCWarning(lcActivePlaylist) << "starting radio failed for" << sourceId << ":" << e.what();
        // No network — or no answer, and the service found offline meanwhile:
        // what's on the disk plays; the station comes later.
        if (e.isOffline() || (offline_ && offline_(sourceId)))
            startOfflineRadio(context, client->sourceName());
        else
            messages_.failure(QString(), e);
    } catch (const std::exception& e) {
        qCWarning(lcActivePlaylist) << "starting radio failed for" << sourceId << ":" << e.what();
        messages_.failure(QString(), e);
    }
    startingRadio_ = false;
    emit startingRadioChanged(false);
}

QVector<Playback::QueueEntry> ActivePlaylist::restoredQueue(int& baseIndex) const
{
    QVector<Playback::QueueEntry> entries = tracks_;
    int added = 0;
    for (const auto& [position, entry] : restoredInputs_) {
        const int target = qBound(0, position + added, int(entries.size()));
        entries.insert(target, entry);
        if (target <= baseIndex)
            ++baseIndex;
        ++added;
    }
    return entries;
}

void ActivePlaylist::reportLocalInputError()
{
    messages_.error(tr("Couldn't open the audio file. Check its permissions."));
}

void ActivePlaylist::playLocalInput(const Track& track, const QString& url, bool persistent)
{
    Playback::QueueEntry input { QStringLiteral("local-file"), track, true, url, persistent };
    if (!context_.isValid() && persistent) {
        const Playlist opened { QStringLiteral("opened-audio"), tr("Opened audio"), std::nullopt, std::nullopt, 1,
            QStringLiteral("playlist") };
        setContext({ input.sourceId, opened });
    }
    if (playback_.hasQueue()) {
        playback_.playLocalInput(std::move(input));
        return;
    }
    int index = tracks_.isEmpty() ? -1 : resumeIndex();
    auto entries = restoredQueue(index);
    // A cached playlist can be visible while its playback queue is still empty.
    // Keep its tracks when the audio request arrives before the source starts.
    const int position = index + 1;
    entries.insert(position, std::move(input));
    if (context_.isRadio()) {
        playback_.loadRadioQueue(context_.sourceId, QString(), std::move(entries), position);
        if (sourceAnswers(context_.sourceId))
            attachStation(context_.sourceId, context_.playlist.id).detach();
    } else {
        playback_.loadQueue(entries, position);
    }
}

void ActivePlaylist::play()
{
    if (!context_.isValid())
        return;
    if (context_.isRadio()) {
        const std::optional<Track> resumeTrack = !playback_.hasQueue() && !tracks_.isEmpty()
            ? std::optional<Track>(tracks_[resumeIndex()].track)
            : std::nullopt;
        if (resumeTrack)
            resumePosition();
        // The source can't start a station yet: what's saved of it plays —
        // or, with nothing saved and the source not merely starting, what's
        // on the disk.
        if (resumeTrack && !sourceAnswers(context_.sourceId))
            playSavedRadio(resumeIndex());
        else if (!resumeTrack && !sourceAnswers(context_.sourceId) && !sourceManager_.isStarting(context_.sourceId))
            startOfflineRadio(context_, sources_.model().sourceName(context_.sourceId));
        else
            startRadio(context_.sourceId, context_.playlist.id, context_, resumeTrack).detach();
    } else if (playback_.hasQueue()) {
        const bool stopped = !playback_.hasCurrentTrack() && savedRow() >= 0;
        if (stopped)
            resumePosition();
        playback_.playAt(stopped ? savedRow() : 0);
    } else if (!restoredInputs_.isEmpty()) {
        resumePosition();
        int index = resumeIndex();
        auto queue = restoredQueue(index);
        playback_.loadQueue(queue, qMin(index, int(queue.size()) - 1));
        emit activated();
    } else {
        resumePosition();
        activate(context_, tracks_, resumeIndex());
    }
}

void ActivePlaylist::restartRadio()
{
    if (context_.isRadio())
        play();
}

void ActivePlaylist::playRow(int row)
{
    // The main list mirrors the queue once there is one (see entries()),
    // so its rows are queue indices.
    if (playback_.hasQueue()) {
        playback_.playAt(row);
    } else if (!restoredInputs_.isEmpty()) {
        auto queue = entries();
        if (row < 0 || row >= queue.size())
            return;
        if (context_.isRadio()) {
            playback_.loadRadioQueue(context_.sourceId, QString(), std::move(queue), row);
            if (sourceAnswers(context_.sourceId))
                attachStation(context_.sourceId, context_.playlist.id).detach();
        } else {
            playback_.loadQueue(queue, row);
        }
    } else if (context_.isRadio() && row >= 0 && row < tracks_.size() && !sourceAnswers(context_.sourceId))
        playSavedRadio(row); // the station attaches once its source is up (attachStation())
    else if (context_.isRadio() && row >= 0 && row < tracks_.size())
        startRadio(context_.sourceId, context_.playlist.id, context_, tracks_[row].track).detach();
    else
        activate(context_, tracks_, row);
}

Rpc::Task<void> ActivePlaylist::loadTracks(PlaylistContext context)
{
    if (context.isRadio())
        co_return;
    QVector<Playback::QueueEntry> entries;
    bool loaded = true;
    if (context.isHistory) {
        for (const History::HistoryEntry& e : history_.entries())
            entries.append(Playback::QueueEntry { e.sourceId, e.track });
    } else {
        loading_ = true;
        emit loadingChanged(true);
        try {
            entries = co_await App::fetchTracks(
                sourceManager_, trackStates_, coverArtCache_, context.sourceId, context.playlist, &playlistTracks_);
        } catch (const std::exception& e) {
            qCWarning(lcActivePlaylist) << "loading tracks failed for" << context.sourceId << ":" << e.what();
            loaded = false;
        }
        loading_ = false;
        emit loadingChanged(false);
    }
    // Only if it's still the active playlist.
    if (!context_.sameAs(context))
        co_return;
    tracksStale_ = !loaded;
    if (loaded) {
        tracks_ = entries;
        saveCachedTracks();
        emit entriesChanged();
    }
    // History resumes once its song's source is up (see the constructor).
    if (!context.isHistory)
        resumePlayback(); // the cached tracks, if the fresh ones failed
}

Rpc::Task<void> ActivePlaylist::refreshRadioLikes(QString sourceId, QString playlistId)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr
        || !client->capabilities()
            .value(QStringLiteral("browse"))
            .toObject()
            .value(QStringLiteral("likedTracks"))
            .toBool())
        co_return;
    const quint64 generation = ++likesRefreshGeneration_;
    const quint64 feedbackRevision = feedbackRevision_;
    QSet<QString> likedIds;
    QSet<QString> seenCursors;
    std::optional<QString> cursor;
    try {
        while (true) {
            const ListLikedResult result = co_await Rpc::catalogListLiked(*client, ListLikedParams { cursor });
            for (const Track& track : result.tracks)
                likedIds.insert(track.id);
            if (!result.nextCursor || result.nextCursor->isEmpty() || seenCursors.contains(*result.nextCursor))
                break;
            seenCursors.insert(*result.nextCursor);
            cursor = result.nextCursor;
        }
    } catch (const std::exception& e) {
        qCWarning(lcActivePlaylist) << "loading liked tracks failed for" << sourceId << ":" << e.what();
        co_return;
    }
    if (generation != likesRefreshGeneration_ || feedbackRevision != feedbackRevision_ || !context_.isRadio()
        || context_.sourceId != sourceId || context_.playlist.id != playlistId)
        co_return;
    QList<Track> refreshed;
    for (const Playback::QueueEntry& entry : tracks_) {
        if (entry.sourceId != sourceId)
            continue;
        Track track = entry.track;
        track.liked = likedIds.contains(track.id);
        if (track.liked.value())
            track.disliked = false;
        refreshed.append(track);
    }
    trackStates_.observe(sourceId, refreshed);
    saveCachedTracks();
}

void ActivePlaylist::restoreFrom(const QString& sourceId, const QList<Playlist>& playlists)
{
    if (pendingRestore_.sourceId != sourceId)
        return;
    const QString playlistId = pendingRestore_.playlistId;
    pendingRestore_ = Config::Settings::ActivePlaylistRef();
    disconnect(&sources_, &Sources::playlistsLoaded, this, &ActivePlaylist::restoreFrom);
    // Unless something else became active in the meantime.
    if (context_.isValid() && (context_.sourceId != sourceId || context_.playlist.id != playlistId))
        return;
    const bool resume = resumePending_;
    for (const Playlist& playlist : playlists) {
        if (playlist.id != playlistId)
            continue;
        setContext(PlaylistContext { sourceId, playlist });
        // The restore itself isn't something else made active.
        resumePending_ = resume;
        if (context_.isRadio()) {
            resumePlayback();
        } else if (refreshDelayMs_ > 0 && !tracks_.isEmpty()) {
            // What's saved will do for now, and plays right away if it has to.
            resumePlayback();
            const PlaylistContext restored = context_;
            QTimer::singleShot(refreshDelayMs_, this, [this, restored]() {
                if (context_.sameAs(restored))
                    loadTracks(context_).detach();
            });
        } else {
            loadTracks(context_).detach();
        }
        return;
    }
    // A radio started from a track isn't among the source's playlists:
    // what loadCachedTracks() brought back is all there is of it.
    if (context_.isValid() && context_.isRadio() && context_.sourceId == sourceId
        && context_.playlist.id == playlistId) {
        resumePending_ = resume;
        resumePlayback();
        return;
    }
    resumePending_ = false; // the playlist is gone
}

void ActivePlaylist::savePosition()
{
    // Only of the track that's saved as the one left off at (not one someone
    // queued into a radio).
    if (playback_.hasCurrentTrack() && playback_.currentTrack().id == settings_.lastActiveTrackId())
        settings_.setLastActivePositionMs(playback_.positionMs());
}

void ActivePlaylist::resumePosition()
{
    playback_.resumeAt(settings_.lastActiveTrackId(), settings_.lastActivePositionMs());
}

void ActivePlaylist::resumePlayback()
{
    if (!resumePending_)
        return;
    resumePending_ = false;
    // The user got something playing first.
    if (playback_.hasQueue())
        return;
    if (!context_.isRadio() && tracks_.isEmpty())
        return;
    resumePosition();
    qCInfo(lcActivePlaylist) << "resuming playback of" << context_.playlist.id;
    play();
}

void ActivePlaylist::resumeFromCache()
{
    if (!resumePending_ || !context_.isValid() || playback_.hasQueue())
        return;
    if (!lastLocalInputId_.isEmpty() && !restoredInputs_.isEmpty()) {
        int index = resumeIndex();
        auto queue = restoredQueue(index);
        const auto found = std::find_if(queue.cbegin(), queue.cend(),
            [this](const auto& entry) { return !entry.localInput.isEmpty() && entry.track.id == lastLocalInputId_; });
        if (found != queue.cend()) {
            const int start = int(found - queue.cbegin());
            resumePending_ = false;
            resumePosition();
            if (context_.isRadio())
                playback_.loadRadioQueue(context_.sourceId, QString(), std::move(queue), start);
            else
                playback_.loadQueue(queue, start);
            return;
        }
    }
    if (tracks_.isEmpty())
        return;
    const int index = resumeIndex();
    const Playback::QueueEntry& entry = tracks_[index];
    if (!playback_.preloader().isCached(entry.sourceId, entry.track.id))
        return;
    resumePending_ = false;
    qCInfo(lcActivePlaylist) << "resuming playback of" << context_.playlist.id << "from the cache";
    resumePosition();
    // Not play()/activate(): the restore of the playlist (pendingRestore_)
    // is still to come, and freshens its tracks behind the playing queue.
    if (context_.isRadio())
        playSavedRadio();
    else {
        int start = index;
        const auto queue = restoredQueue(start);
        playback_.loadQueue(queue, start);
    }
}

bool ActivePlaylist::sourceAnswers(const QString& sourceId) const
{
    const Rpc::RpcClient* client = sourceManager_.client(sourceId);
    return client != nullptr && client->available();
}

void ActivePlaylist::startOfflineRadio(const PlaylistContext& context, const QString& sourceName)
{
    if (playOfflineRadio(context))
        return;
    // Asked for, and nothing happens: that is to be said, offline indicator
    // or not.
    messages_.error(tr("No network connection, and nothing of %1 is downloaded to play").arg(sourceName));
}

bool ActivePlaylist::playOfflineRadio(const PlaylistContext& context)
{
    // Its own saved tracks, if any of them can play.
    if (context_.sameAs(context)) {
        for (const Playback::QueueEntry& entry : tracks_) {
            if (playback_.preloader().isCached(entry.sourceId, entry.track.id)) {
                playSavedRadio();
                return true;
            }
        }
    }
    const QList<Track> tracks = cachedRadioTracks(context.sourceId, Playback::PlaybackController::kOfflineRadioBatch);
    if (tracks.isEmpty())
        return false;
    qCInfo(lcActivePlaylist) << "no station for" << context.playlist.id << ": playing downloaded tracks";
    setContext(context);
    // No station: it's attached once the source is up (attachStation()).
    playback_.startRadio(context.sourceId, QString(), tracks, 0);
    messages_.offline(tr("No network connection: playing downloaded tracks"));
    return true;
}

QList<Track> ActivePlaylist::cachedRadioTracks(const QString& sourceId, int count) const
{
    // Not again what's queued to come, nor what's playing.
    QSet<QString> queued;
    const QVector<Playback::QueueEntry>& queue = playback_.queue();
    for (int i = qMax(0, playback_.currentIndex()); i < queue.size(); ++i)
        queued.insert(queue[i].track.id);
    const auto usable = [&](const Track& track) {
        if (queued.contains(track.id) || !playback_.preloader().isCached(sourceId, track.id))
            return false;
        const Library::TrackState state = trackStates_.state(sourceId, track.id);
        return !state.disliked.value_or(false) && !state.unavailable;
    };
    // History newest first, then the radio's own tracks and the queue: the
    // order of when each played, the latest first.
    QList<Track> candidates;
    QSet<QString> seen;
    const auto consider = [&](const Track& track) {
        if (!seen.contains(track.id) && usable(track))
            candidates.append(track);
        seen.insert(track.id);
    };
    for (const History::HistoryEntry& entry : history_.entries()) {
        if (entry.sourceId == sourceId)
            consider(entry.track);
    }
    for (const Playback::QueueEntry& entry : tracks_) {
        if (entry.sourceId == sourceId)
            consider(entry.track);
    }
    for (const Playback::QueueEntry& entry : queue) {
        if (entry.sourceId == sourceId)
            consider(entry.track);
    }
    // The ones played lately wait, as a station holds them back.
    const QList<Track> recent = history_.recentTracks(sourceId, settings_.sourceRadioRepeatGap(sourceId));
    QSet<QString> recentIds;
    for (const Track& track : recent)
        recentIds.insert(track.id);
    QList<Track> fresh;
    QList<Track> lately;
    for (const Track& track : candidates)
        (recentIds.contains(track.id) ? lately : fresh).append(track);
    std::shuffle(fresh.begin(), fresh.end(), *QRandomGenerator::global());
    QList<Track> picked = fresh.mid(0, count);
    // Few enough downloaded to run out: the least lately played come again.
    for (int i = int(lately.size()) - 1; i >= 0 && picked.size() < count; --i)
        picked.append(lately[i]);
    return picked;
}

void ActivePlaylist::playSavedRadio(int index)
{
    int start = index >= 0 ? index : resumeIndex();
    auto queue = restoredQueue(start);
    playback_.loadRadioQueue(context_.sourceId, QString(), std::move(queue), start);
}

Rpc::Task<void> ActivePlaylist::attachStation(QString sourceId, QString seed)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    try {
        StartRadioResult result = co_await Rpc::catalogStartRadio(*client, radioParams(sourceId, seed));
        // Unless something else has been started meanwhile.
        if (!context_.isRadio() || context_.sourceId != sourceId || context_.playlist.id != seed
            || !playback_.awaitsStation())
            co_return;
        trackStates_.observe(sourceId, result.initialTracks);
        coverArtCache_.assignSource(sourceId, result.initialTracks);
        playback_.attachStation(sourceId, result.stationId, result.initialTracks);
    } catch (const std::exception& e) {
        // Tried again when the source lists its playlists next.
        qCWarning(lcActivePlaylist) << "starting the resumed radio failed for" << sourceId << ":" << e.what();
    }
}

void ActivePlaylist::dropTracks(const QString& sourceId, const QStringList& trackIds)
{
    if (context_.isHistory || context_.sourceId != sourceId || playback_.hasQueue())
        return;
    const qsizetype removed
        = tracks_.removeIf([&](const Playback::QueueEntry& entry) { return trackIds.contains(entry.track.id); });
    if (removed == 0)
        return;
    saveCachedTracks();
    emit entriesChanged();
}

void ActivePlaylist::applyPlaylistEdit(
    const QString& sourceId, const Track& track, const QString& playlistId, bool added, int trackCount)
{
    if (context_.isHistory || context_.sourceId != sourceId || context_.playlist.id != playlistId)
        return;
    context_.playlist.trackCount = trackCount;
    // Only the not-yet-queued tracks — a running queue stays as it is.
    if (playback_.hasQueue())
        return;
    if (added) {
        tracks_.append(Playback::QueueEntry { sourceId, track });
    } else {
        for (int i = 0; i < tracks_.size(); ++i) {
            if (tracks_[i].track.id == track.id) {
                tracks_.removeAt(i);
                break;
            }
        }
    }
    saveCachedTracks();
    emit entriesChanged();
}

} // namespace ViewModel
