#include "TrackListModel.h"

#include <QUrl>

#include "CoverArtCache.h"
#include "Preload.h"
#include "TrackStates.h"

namespace ViewModel {

namespace {
QString coverUrlOf(const Track& track)
{
    return track.coverUrl.value_or(track.album ? track.album->coverUrl.value_or(QString()) : QString());
}
} // namespace

TrackListModel::TrackListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void TrackListModel::setTracks(const QString& sourceId, const QList<Track>& tracks)
{
    beginResetModel();
    sourceId_ = sourceId;
    tracks_ = QVector<Track>(tracks.begin(), tracks.end());
    mixedSourceIds_.clear();
    playedAt_.clear();
    mixedSource_ = false;
    endResetModel();
}

void TrackListModel::setMixedSourceTracks(const QList<MixedSourceEntry>& entries)
{
    beginResetModel();
    sourceId_.clear();
    tracks_.clear();
    mixedSourceIds_.clear();
    playedAt_.clear();
    tracks_.reserve(entries.size());
    mixedSourceIds_.reserve(entries.size());
    playedAt_.reserve(entries.size());
    for (const MixedSourceEntry& entry : entries) {
        tracks_.append(entry.track);
        mixedSourceIds_.append(entry.sourceId);
        playedAt_.append(entry.playedAt);
    }
    mixedSource_ = true;
    endResetModel();
}

void TrackListModel::appendTracks(const QList<Track>& tracks)
{
    if (tracks.isEmpty())
        return;
    const int first = tracks_.size();
    beginInsertRows(QModelIndex(), first, first + tracks.size() - 1);
    for (const Track& t : tracks)
        tracks_.append(t);
    endInsertRows();
}

void TrackListModel::appendEntry(const QString& sourceId, const Track& track)
{
    const int row = tracks_.size();
    beginInsertRows(QModelIndex(), row, row);
    tracks_.append(track);
    if (mixedSource_) {
        mixedSourceIds_.append(sourceId);
        playedAt_.append(QDateTime());
    }
    endInsertRows();
}

void TrackListModel::removeFirst(const QString& sourceId, const QString& trackId)
{
    for (int row = 0; row < tracks_.size(); ++row) {
        if (tracks_[row].id != trackId || sourceIdAt(row) != sourceId)
            continue;
        beginRemoveRows(QModelIndex(), row, row);
        tracks_.removeAt(row);
        if (mixedSource_) {
            mixedSourceIds_.removeAt(row);
            playedAt_.removeAt(row);
        }
        endRemoveRows();
        return;
    }
}

void TrackListModel::clear()
{
    beginResetModel();
    sourceId_.clear();
    tracks_.clear();
    mixedSourceIds_.clear();
    playedAt_.clear();
    mixedSource_ = false;
    endResetModel();
}

QString TrackListModel::sourceIdAt(int row) const { return mixedSource_ ? mixedSourceIds_[row] : sourceId_; }

void TrackListModel::setTrackStates(Library::TrackStates* states)
{
    states_ = states;
    connect(states_, &Library::TrackStates::changed, this, &TrackListModel::onStateChanged);
    connect(states_, &Library::TrackStates::bulkChanged, this, [this]() {
        if (!tracks_.isEmpty())
            emit dataChanged(index(0), index(tracks_.size() - 1),
                { LikedRole, DislikedRole, LastPlayedRole, StreamQualityRole, UnavailableRole });
    });
}

void TrackListModel::setPreload(Preload* preload)
{
    preload_ = preload;
    connect(preload_, &Preload::trackChanged, this, [this](const QString& sourceId, const QString& trackId) {
        for (int row = 0; row < tracks_.size(); ++row) {
            if (tracks_[row].id == trackId && sourceIdAt(row) == sourceId) {
                const QModelIndex idx = index(row);
                emit dataChanged(idx, idx, { CachedRole, DownloadProgressRole });
            }
        }
    });
    // The cache dropped or gained something — which tracks, it doesn't say.
    connect(preload_, &Preload::cacheUsedChanged, this, [this]() {
        if (!tracks_.isEmpty())
            emit dataChanged(index(0), index(tracks_.size() - 1), { CachedRole });
    });
}

void TrackListModel::setCovers(Covers::CoverArtCache* covers)
{
    covers_ = covers;
    connect(covers_, &Covers::CoverArtCache::pixmapReady, this, [this](const QString& url) {
        for (int row = 0; row < tracks_.size(); ++row) {
            if (coverUrlOf(tracks_[row]) == url) {
                const QModelIndex idx = index(row);
                emit dataChanged(idx, idx, { CoverFileRole });
            }
        }
    });
}

void TrackListModel::setCurrentTrack(const QString& sourceId, const QString& trackId)
{
    if (sourceId == currentSourceId_ && trackId == currentTrackId_)
        return;
    currentSourceId_ = sourceId;
    currentTrackId_ = trackId;
    if (!tracks_.isEmpty())
        emit dataChanged(index(0), index(int(tracks_.size()) - 1), { IsCurrentRole });
}

void TrackListModel::onStateChanged(const QString& sourceId, const QString& trackId)
{
    for (int row = 0; row < tracks_.size(); ++row) {
        if (tracks_[row].id == trackId && sourceIdAt(row) == sourceId) {
            const QModelIndex idx = index(row);
            emit dataChanged(idx, idx, { LikedRole, DislikedRole, LastPlayedRole, StreamQualityRole, UnavailableRole });
        }
    }
}

int TrackListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return tracks_.size();
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        { SourceIdRole, "sourceId" },
        { TrackIdRole, "trackId" },
        { TitleRole, "title" },
        { ArtistRole, "artist" },
        { AlbumRole, "album" },
        { DurationMsRole, "durationMs" },
        { ExplicitRole, "isExplicit" },
        { WebUrlRole, "webUrl" },
        { CoverFileRole, "coverUrl" },
        { IsCurrentRole, "isCurrent" },
        { LikedRole, "liked" },
        { DislikedRole, "disliked" },
        { UnavailableRole, "unavailable" },
        { CachedRole, "cached" },
        { DownloadProgressRole, "downloadProgress" },
        { PlayedAtRole, "playedAt" },
        { LastPlayedRole, "lastPlayedAt" },
    };
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= tracks_.size())
        return { };
    const Track& t = tracks_[index.row()];
    switch (role) {
        case TrackRole:
            return QVariant::fromValue(t);
        case SourceIdRole:
            return sourceIdAt(index.row());
        case PlayedAtRole:
            return mixedSource_ ? QVariant::fromValue(playedAt_[index.row()]) : QVariant();
        case UnavailableRole:
            return states_ != nullptr && states_->state(sourceIdAt(index.row()), t.id).unavailable;
        case StreamQualityRole: {
            if (states_ == nullptr)
                return { };
            const auto quality = states_->state(sourceIdAt(index.row()), t.id).streamQuality;
            return quality.has_value() ? QVariant::fromValue(*quality) : QVariant();
        }
        case LikedRole:
        case DislikedRole:
        case LastPlayedRole: {
            if (states_ == nullptr)
                return { };
            const Library::TrackState state = states_->state(sourceIdAt(index.row()), t.id);
            if (role == LastPlayedRole)
                return state.lastPlayedAt.isValid() ? QVariant::fromValue(state.lastPlayedAt) : QVariant();
            const std::optional<bool>& flag = role == LikedRole ? state.liked : state.disliked;
            return flag.has_value() ? QVariant(*flag) : QVariant();
        }
        case CachedRole:
            if (preload_ == nullptr)
                return { };
            return preload_->stateFor(sourceIdAt(index.row()), t.id) == Preload::State::Ready;
        case DownloadProgressRole:
            if (preload_ == nullptr
                || preload_->stateFor(sourceIdAt(index.row()), t.id) != Preload::State::Downloading)
                return { };
            return preload_->fractionFor(sourceIdAt(index.row()), t.id);
        case Qt::DisplayRole:
        case TitleRole:
            return t.title;
        case TrackIdRole:
            return t.id;
        case IsCurrentRole:
            return !currentTrackId_.isEmpty() && t.id == currentTrackId_
                && sourceIdAt(index.row()) == currentSourceId_;
        case ArtistRole: {
            QStringList names;
            for (const Artist& artist : t.artists)
                names.append(artist.name);
            return names.join(QStringLiteral(", "));
        }
        case AlbumRole:
            return t.album ? t.album->title : QString();
        case DurationMsRole:
            return t.durationMs;
        case ExplicitRole:
            return t.explicit_.value_or(false);
        case WebUrlRole:
            return t.webUrl.value_or(QString());
        case CoverFileRole: {
            if (covers_ == nullptr)
                return QString();
            const QString file = covers_->localFile(coverUrlOf(t));
            return file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString();
        }
        default:
            return { };
    }
}

} // namespace ViewModel
