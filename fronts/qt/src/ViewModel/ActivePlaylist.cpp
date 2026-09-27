#include "ActivePlaylist.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>

#include "CoverArtCache.h"
#include "Messages.h"
#include "PlaybackHistory.h"
#include "PlaylistEditing.h"
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

QString cachedTracksPath(const Config::Settings& settings)
{
    return QFileInfo(settings.filePath()).absolutePath() + QStringLiteral("/active-playlist.json");
}
}

ActivePlaylist::ActivePlaylist(Playback::PlaybackController& playback, Rpc::SourceManager& sourceManager,
    Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, History::PlaybackHistory& history,
    Config::Settings& settings, Sources& sources, App::PlaylistEditing& playlistEditing, Messages& messages,
    QObject* parent)
    : QObject(parent)
    , playback_(playback)
    , sourceManager_(sourceManager)
    , trackStates_(trackStates)
    , coverArtCache_(coverArtCache)
    , history_(history)
    , settings_(settings)
    , sources_(sources)
    , messages_(messages)
{
    connect(&playback_, &Playback::PlaybackController::queueChanged, this, [this]() {
        // A radio's track list grows through notifications, outside loadTracks().
        if (context_.isRadio() && playback_.isRadio())
            saveCachedTracks();
        emit entriesChanged();
    });
    connect(&playback_, &Playback::PlaybackController::trackChanged, this,
        [this](const Track& track, const QString& sourceId) {
            if (!context_.persistent || !context_.isValid())
                return;
            if (!context_.isHistory && sourceId != context_.sourceId)
                return;
            if (context_.isRadio() && playback_.queue()[playback_.currentIndex()].userQueued)
                return;
            settings_.setLastActiveTrack(track.id, playback_.currentIndex());
        });
    connect(&playlistEditing, &App::PlaylistEditing::playlistEdited, this, &ActivePlaylist::applyPlaylistEdit);

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
        if (context_.isRadio() && context_.sourceId == sourceId && !tracks_.isEmpty())
            refreshRadioLikes(sourceId, context_.playlist.id).detach();
    });
    if (saved.kind == QStringLiteral("history")) {
        if (!context_.isValid())
            setContext(historyContext());
        loadTracks(context_).detach();
    } else if (!saved.sourceId.isEmpty() && !saved.playlistId.isEmpty()) {
        pendingRestore_ = saved;
        connect(&sources_, &Sources::playlistsLoaded, this, &ActivePlaylist::restoreFrom);
    }
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
    } catch (const Rpc::ProtocolParseError&) {
        context_ = { };
        tracks_.clear();
    }
}

void ActivePlaylist::saveCachedTracks() const
{
    if (!context_.persistent || !context_.isValid())
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
    const QJsonObject data { { QStringLiteral("sourceId"), context_.sourceId },
        { QStringLiteral("playlistId"), context_.playlist.id },
        { QStringLiteral("kind"), context_.isHistory ? QStringLiteral("history") : context_.playlist.kind },
        { QStringLiteral("playlist"), context_.playlist.toJson() }, { QStringLiteral("tracks"), rows } };
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
    return playback_.hasQueue() ? playback_.queue() : tracks_;
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

std::optional<Playback::QueueEntry> ActivePlaylist::savedEntry() const
{
    const QString savedId = settings_.lastActiveTrackId();
    if (savedId.isEmpty() || tracks_.isEmpty())
        return std::nullopt;
    const int index = resumeIndex();
    if (tracks_[index].track.id != savedId)
        return std::nullopt;
    return tracks_[index];
}

void ActivePlaylist::setContext(const PlaylistContext& context)
{
    const bool changed = !context_.sameAs(context);
    context_ = context;
    if (changed)
        tracks_.clear();
    sources_.model().setActivePlaylist(context.isHistory ? QString() : context.sourceId,
        context.isHistory ? QStringLiteral("history") : context.playlist.id);
    if (context.persistent) {
        settings_.setLastActivePlaylist({ context.isHistory ? QString() : context.sourceId, context.playlist.id,
            context.isHistory ? QStringLiteral("history") : context.playlist.kind });
    }
    // Something the user made active wins over what's still to restore.
    pendingRestore_ = Config::Settings::ActivePlaylistRef();
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
    try {
        const QVector<Playback::QueueEntry> entries
            = co_await App::fetchTracks(sourceManager_, trackStates_, coverArtCache_, sourceId, playlist);
        activate(context, entries, 0);
    } catch (const std::exception& e) {
        qCWarning(lcActivePlaylist) << "loading tracks failed for" << sourceId << ":" << e.what();
        messages_.error(QString::fromStdString(e.what()));
    }
}

Rpc::Task<void> ActivePlaylist::startRadio(
    QString sourceId, QString seed, PlaylistContext context, std::optional<Track> resumeTrack)
{
    Rpc::RpcClient* client = sourceManager_.client(sourceId);
    if (client == nullptr)
        co_return;
    startingRadio_ = true;
    emit startingRadioChanged(true);
    try {
        StartRadioParams params { seed };
        StartRadioResult result = co_await Rpc::catalogStartRadio(*client, params);
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
    } catch (const std::exception& e) {
        qCWarning(lcActivePlaylist) << "starting radio failed for" << sourceId << ":" << e.what();
        messages_.error(QString::fromStdString(e.what()));
    }
    startingRadio_ = false;
    emit startingRadioChanged(false);
}

void ActivePlaylist::play()
{
    if (!context_.isValid())
        return;
    if (context_.isRadio()) {
        const std::optional<Track> resumeTrack = !playback_.hasQueue() && !tracks_.isEmpty()
            ? std::optional<Track>(tracks_[resumeIndex()].track)
            : std::nullopt;
        startRadio(context_.sourceId, context_.playlist.id, context_, resumeTrack).detach();
    } else if (playback_.hasQueue())
        playback_.playAt(0);
    else
        activate(context_, tracks_, resumeIndex());
}

void ActivePlaylist::playRow(int row)
{
    // The main list mirrors the queue once there is one (see entries()),
    // so its rows are queue indices.
    if (playback_.hasQueue())
        playback_.playAt(row);
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
                sourceManager_, trackStates_, coverArtCache_, context.sourceId, context.playlist);
        } catch (const std::exception& e) {
            qCWarning(lcActivePlaylist) << "loading tracks failed for" << context.sourceId << ":" << e.what();
            loaded = false;
        }
        loading_ = false;
        emit loadingChanged(false);
    }
    // Only if it's still the active playlist.
    if (!context_.sameAs(context) || !loaded)
        co_return;
    tracks_ = entries;
    saveCachedTracks();
    emit entriesChanged();
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
    for (const Playlist& playlist : playlists) {
        if (playlist.id != playlistId)
            continue;
        setContext(PlaylistContext { sourceId, playlist });
        if (!context_.isRadio())
            loadTracks(context_).detach();
        break;
    }
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
