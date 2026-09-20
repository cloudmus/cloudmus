#include "TrackListModel.h"

namespace Ui {

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

void TrackListModel::markTrackLiked(const QString& sourceId, const QString& trackId, bool liked)
{
    for (int row = 0; row < tracks_.size(); ++row) {
        if (sourceIdAt(row) == sourceId && tracks_[row].id == trackId) {
            tracks_[row].liked = liked;
            const QModelIndex idx = index(row);
            emit dataChanged(idx, idx, { TrackRole });
        }
    }
}

int TrackListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return tracks_.size();
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
        case Qt::DisplayRole:
            return t.title;
        default:
            return { };
    }
}

} // namespace Ui
