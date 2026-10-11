#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <optional>

#include "Models.h"

namespace Library {

// What a source last streamed a track as (StreamDescriptor's codec and
// bitrateKbps). Either part is empty/0 when the source didn't say.
struct StreamQuality {
    QString codec; // lowercase short name: "mp3", "aac", "opus"
    int bitrateKbps = 0;

    bool operator==(const StreamQuality&) const = default;
};

// What the app knows about a track beyond its metadata. liked/disliked
// are unset while no source has said anything about them.
struct TrackState {
    std::optional<bool> liked;
    std::optional<bool> disliked;
    QDateTime lastPlayedAt; // UTC; invalid if never played (per History)
    // Known only for tracks whose stream was resolved in this run.
    std::optional<StreamQuality> streamQuality;
    // The source could not play it in this run (removed, blocked in the
    // region): views show it dimmed, and playback skips it.
    bool unavailable = false;
};

// The one place a track's like/dislike/last-played state lives, keyed by
// (sourceId, trackId). Every view — track lists, the sheet, History, the
// now-playing bar, context menus — reads it from here at paint/open time
// instead of from its own Track copy, so a like made anywhere, or a fresh
// value a source reports with a playlist, shows up everywhere at once.
//
// Fed by: every Track a source returns (observe(): its liked/disliked and
// the audio's codec and bitrate when it carries them), successful
// feedback.like/dislike calls (setLiked()/setDisliked()), playback
// history (setLastPlayed()), and the streams playback resolves
// (setStreamQuality()).
class TrackStates : public QObject {
    Q_OBJECT

public:
    explicit TrackStates(QObject* parent = nullptr);

    TrackState state(const QString& sourceId, const QString& trackId) const;

    // Takes the liked/disliked values a source reported with `track`, when
    // present — the source is the authority, so they overwrite what's here.
    // With `onlyIfUnknown`, a value is only filled in where none is known
    // yet (for stale copies, e.g. History's saved snapshots).
    void observe(const QString& sourceId, const Track& track, bool onlyIfUnknown = false);
    void observe(const QString& sourceId, const QList<Track>& tracks);

    // A successful feedback call. Like and dislike are mutually exclusive
    // (the services cross-clear them — docs/protocol.md §7.4).
    void setLiked(const QString& sourceId, const QString& trackId, bool liked);
    void setDisliked(const QString& sourceId, const QString& trackId, bool disliked);
    // Keeps the latest of the known and the given time.
    void setLastPlayed(const QString& sourceId, const QString& trackId, const QDateTime& playedAtUtc);
    // What the source streamed the track as. Nothing to record when it
    // said neither codec nor bitrate.
    void setStreamQuality(const QString& sourceId, const QString& trackId, const StreamQuality& quality);
    // The source said the track can't be played (reason "unavailable",
    // docs/protocol.md §9.1). Lasts for the run.
    void setUnavailable(const QString& sourceId, const QString& trackId);

signals:
    // One track's state changed.
    void changed(const QString& sourceId, const QString& trackId);
    // A successful user like/dislike action should also update cached playlists.
    void feedbackChanged(const QString& sourceId, const QString& trackId);
    // Many may have (a whole list observed) — views just refresh everything.
    void bulkChanged();

private:
    static QString key(const QString& sourceId, const QString& trackId) { return sourceId + QChar(0x1f) + trackId; }
    bool merge(const QString& sourceId, const Track& track, bool onlyIfUnknown);

    QHash<QString, TrackState> states_;
};

} // namespace Library

Q_DECLARE_METATYPE(Library::StreamQuality)
