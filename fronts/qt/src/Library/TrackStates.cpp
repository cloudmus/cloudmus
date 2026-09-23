#include "TrackStates.h"

namespace Library {

TrackStates::TrackStates(QObject* parent)
    : QObject(parent)
{
}

TrackState TrackStates::state(const QString& sourceId, const QString& trackId) const
{
    return states_.value(key(sourceId, trackId));
}

bool TrackStates::merge(const QString& sourceId, const Track& track, bool onlyIfUnknown)
{
    if (!track.liked.has_value() && !track.disliked.has_value())
        return false;
    TrackState& s = states_[key(sourceId, track.id)];
    bool changed = false;
    const auto apply = [&](std::optional<bool>& field, const std::optional<bool>& value) {
        if (!value.has_value() || (onlyIfUnknown && field.has_value()) || field == value)
            return;
        field = value;
        changed = true;
    };
    apply(s.liked, track.liked);
    apply(s.disliked, track.disliked);
    return changed;
}

void TrackStates::observe(const QString& sourceId, const Track& track, bool onlyIfUnknown)
{
    if (merge(sourceId, track, onlyIfUnknown))
        emit changed(sourceId, track.id);
}

void TrackStates::observe(const QString& sourceId, const QList<Track>& tracks)
{
    bool any = false;
    for (const Track& track : tracks)
        any = merge(sourceId, track, /*onlyIfUnknown=*/false) || any;
    if (any)
        emit bulkChanged();
}

void TrackStates::setLiked(const QString& sourceId, const QString& trackId, bool liked)
{
    TrackState& s = states_[key(sourceId, trackId)];
    s.liked = liked;
    if (liked)
        s.disliked = false;
    emit changed(sourceId, trackId);
}

void TrackStates::setDisliked(const QString& sourceId, const QString& trackId, bool disliked)
{
    TrackState& s = states_[key(sourceId, trackId)];
    s.disliked = disliked;
    if (disliked)
        s.liked = false;
    emit changed(sourceId, trackId);
}

void TrackStates::setLastPlayed(const QString& sourceId, const QString& trackId, const QDateTime& playedAtUtc)
{
    TrackState& s = states_[key(sourceId, trackId)];
    if (s.lastPlayedAt.isValid() && s.lastPlayedAt >= playedAtUtc)
        return;
    s.lastPlayedAt = playedAtUtc;
    emit changed(sourceId, trackId);
}

} // namespace Library
