#include "Track.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace Playback {

namespace {
Q_LOGGING_CATEGORY(lcTracks, "cloudmus.playback.tracks")
}

QList<Track> loadTracks(const QString& dir)
{
    QFile file(QDir(dir).filePath(QStringLiteral("tracks.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcTracks) << "no track list:" << file.fileName() << file.errorString();
        return { };
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (!doc.isArray()) {
        qCWarning(lcTracks) << "bad track list:" << file.fileName() << error.errorString();
        return { };
    }
    QList<Track> tracks;
    const QDir base(dir);
    for (const QJsonValue& value : doc.array()) {
        const QJsonObject object = value.toObject();
        Track track;
        track.id = object.value(QStringLiteral("id")).toString();
        track.file = base.filePath(object.value(QStringLiteral("file")).toString());
        track.title = object.value(QStringLiteral("title")).toString();
        track.artist = object.value(QStringLiteral("artist")).toString();
        track.durationMs = object.value(QStringLiteral("durationMs")).toInteger();
        const QString cover = object.value(QStringLiteral("cover")).toString();
        if (!cover.isEmpty())
            track.cover = base.filePath(cover);
        tracks.append(track);
    }
    qCInfo(lcTracks) << tracks.size() << "tracks from" << dir;
    return tracks;
}

} // namespace Playback
