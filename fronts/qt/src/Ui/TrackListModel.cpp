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
    endResetModel();
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
            return sourceId_;
        case Qt::DisplayRole:
            return t.title;
        default:
            return { };
    }
}

} // namespace Ui
