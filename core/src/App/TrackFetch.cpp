#include "TrackFetch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>

#include "ProtocolParseError.h"
#include "CoverArtCache.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "SourceManager.h"
#include "TrackStates.h"

namespace App {

PlaylistTrackCache::PlaylistTrackCache(QString dir)
    : dir_(std::move(dir))
{
}

QString PlaylistTrackCache::fileFor(const QString& sourceId, const QString& playlistId) const
{
    const auto part = [](const QString& s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); };
    return dir_ + QLatin1Char('/') + part(sourceId) + QLatin1Char('/') + part(playlistId) + QStringLiteral(".json");
}

void PlaylistTrackCache::save(
    const QString& sourceId, const QString& playlistId, const QVector<Playback::QueueEntry>& entries) const
{
    if (dir_.isEmpty() || sourceId.isEmpty() || playlistId.isEmpty())
        return;
    QJsonArray rows;
    for (const Playback::QueueEntry& entry : entries) {
        rows.append(QJsonObject {
            { QStringLiteral("sourceId"), entry.sourceId }, { QStringLiteral("track"), entry.track.toJson() } });
    }
    const QString path = fileFor(sourceId, playlistId);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(QJsonObject { { QStringLiteral("tracks"), rows } }).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return;

    // Bounded: the least recently written go.
    QFileInfoList files;
    const QFileInfoList sourceDirs = QDir(dir_).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& sourceDir : sourceDirs)
        files += QDir(sourceDir.absoluteFilePath()).entryInfoList({ QStringLiteral("*.json") }, QDir::Files);
    if (files.size() <= kMaxFiles)
        return;
    std::sort(files.begin(), files.end(),
        [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() < b.lastModified(); });
    for (int i = 0; i < files.size() - kMaxFiles; ++i)
        QFile::remove(files[i].absoluteFilePath());
}

std::optional<QVector<Playback::QueueEntry>> PlaylistTrackCache::load(
    const QString& sourceId, const QString& playlistId) const
{
    if (dir_.isEmpty())
        return std::nullopt;
    QFile file(fileFor(sourceId, playlistId));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject() || !document.object().value(QStringLiteral("tracks")).isArray())
        return std::nullopt;
    QVector<Playback::QueueEntry> entries;
    for (const QJsonValue& row : document.object().value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject entry = row.toObject();
        if (!entry.value(QStringLiteral("track")).isObject())
            continue;
        try {
            entries.append(Playback::QueueEntry { entry.value(QStringLiteral("sourceId")).toString(),
                Track::fromJson(entry.value(QStringLiteral("track")).toObject()) });
        } catch (const Rpc::ProtocolParseError&) {
            continue; // from a version that saved it differently
        }
    }
    if (entries.isEmpty())
        return std::nullopt;
    return entries;
}

void PlaylistTrackCache::removeSource(const QString& sourceId) const
{
    if (dir_.isEmpty() || sourceId.isEmpty())
        return;
    QDir(dir_ + QLatin1Char('/') + QString::fromLatin1(QUrl::toPercentEncoding(sourceId))).removeRecursively();
}

std::optional<QVector<Playback::QueueEntry>> cachedTracks(const PlaylistTrackCache& cache,
    Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, const QString& sourceId,
    const Playlist& playlist)
{
    std::optional<QVector<Playback::QueueEntry>> entries = cache.load(sourceId, playlist.id);
    if (!entries)
        return std::nullopt;
    QList<Track> tracks;
    for (const Playback::QueueEntry& entry : std::as_const(*entries))
        tracks.append(entry.track);
    // What the user did since (liking) wins over the saved copy.
    for (const Track& track : std::as_const(tracks))
        trackStates.observe(sourceId, track, /*onlyIfUnknown=*/true);
    coverArtCache.assignSource(sourceId, tracks);
    return entries;
}

Rpc::Task<QVector<Playback::QueueEntry>> fetchTracks(Rpc::SourceManager& sourceManager,
    Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, QString sourceId, Playlist playlist,
    const PlaylistTrackCache* cache)
{
    QVector<Playback::QueueEntry> entries;
    Rpc::RpcClient* client = sourceManager.client(sourceId);
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
    trackStates.observe(sourceId, tracks);
    coverArtCache.assignSource(sourceId, tracks);
    entries.reserve(tracks.size());
    for (const Track& track : tracks)
        entries.append(Playback::QueueEntry { sourceId, track });
    if (cache != nullptr)
        cache->save(sourceId, playlist.id, entries);
    co_return entries;
}

} // namespace App
