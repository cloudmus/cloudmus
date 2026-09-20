#include "PlaybackHistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "ProtocolParseError.h"

namespace History {

namespace {
// Plenty to scroll back through without the file growing unbounded over
// months of use — each entry is small (a Track plus two strings).
constexpr int kMaxEntries = 500;

QString historyFilePath()
{
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return configHome + QStringLiteral("/cloudmus/fronts/qt/history.json");
}
} // namespace

PlaybackHistory::PlaybackHistory(QObject* parent)
    : QObject(parent)
{
    load();
}

QString PlaybackHistory::filePath() { return historyFilePath(); }

void PlaybackHistory::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return; // no history yet — first run, or nothing played before this feature existed

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return;

    for (const QJsonValue& v : doc.array()) {
        if (!v.isObject())
            continue;
        const QJsonObject obj = v.toObject();
        const QJsonValue trackValue = obj.value(QStringLiteral("track"));
        if (!trackValue.isObject())
            continue;
        // A malformed entry (hand-edited file, future format change) is
        // skipped rather than aborting the whole load — see Track::fromJson
        // in generated/Models.h, which throws Rpc::ProtocolParseError on a
        // missing/mistyped required field.
        try {
            HistoryEntry entry;
            entry.sourceId = obj.value(QStringLiteral("sourceId")).toString();
            entry.playedAt = QDateTime::fromString(obj.value(QStringLiteral("playedAt")).toString(), Qt::ISODate);
            entry.track = Track::fromJson(trackValue.toObject());
            entries_.append(entry);
        } catch (const Rpc::ProtocolParseError&) {
            continue;
        }
    }
}

void PlaybackHistory::save() const
{
    QJsonArray arr;
    for (const HistoryEntry& e : entries_) {
        QJsonObject obj;
        obj.insert(QStringLiteral("sourceId"), e.sourceId);
        obj.insert(QStringLiteral("playedAt"), e.playedAt.toString(Qt::ISODate));
        obj.insert(QStringLiteral("track"), e.track.toJson());
        arr.append(obj);
    }

    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void PlaybackHistory::record(const QString& sourceId, const Track& track)
{
    entries_.prepend(HistoryEntry { sourceId, track, QDateTime::currentDateTimeUtc() });
    while (entries_.size() > kMaxEntries)
        entries_.removeLast();
    save();
    emit changed();
}

void PlaybackHistory::markTrackLiked(const QString& sourceId, const QString& trackId, bool liked)
{
    bool anyMatched = false;
    for (HistoryEntry& e : entries_) {
        if (e.sourceId == sourceId && e.track.id == trackId) {
            e.track.liked = liked;
            anyMatched = true;
        }
    }
    if (anyMatched) {
        save();
        emit changed();
    }
}

} // namespace History
