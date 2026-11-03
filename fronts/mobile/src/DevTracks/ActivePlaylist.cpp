#include "ActivePlaylist.h"

#include <QUrl>

#include "QueuePlayer.h"

namespace ViewModel {

TrackListModel::TrackListModel(Playback::QueuePlayer& player, QObject* parent)
    : QAbstractListModel(parent)
    , player_(player)
    , current_(player.currentIndex())
{
    connect(&player_, &Playback::QueuePlayer::currentChanged, this, [this] {
        const int previous = current_;
        current_ = player_.currentIndex();
        for (int row : { previous, current_ }) {
            if (row >= 0 && row < rowCount())
                emit dataChanged(index(row), index(row), { IsCurrentRole });
        }
    });
}

int TrackListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(player_.tracks().size());
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return { };
    const Playback::Track& track = player_.tracks()[index.row()];
    switch (role) {
        case TrackIdRole:
            return track.id;
        case Qt::DisplayRole:
        case TitleRole:
            return track.title;
        case ArtistRole:
            return track.artist;
        case DurationMsRole:
            return track.durationMs;
        case CoverUrlRole:
            return track.cover.isEmpty() ? QString() : QUrl::fromLocalFile(track.cover).toString();
        case IsCurrentRole:
            return index.row() == current_;
        default:
            return { };
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        { TrackIdRole, "trackId" },
        { TitleRole, "title" },
        { ArtistRole, "artist" },
        { DurationMsRole, "durationMs" },
        { CoverUrlRole, "coverUrl" },
        { IsCurrentRole, "isCurrent" },
        { CachedRole, "cached" },
        { DownloadProgressRole, "downloadProgress" },
        { LikedRole, "liked" },
        { DislikedRole, "disliked" },
        { UnavailableRole, "unavailable" },
        { PlayedAtRole, "playedAt" },
    };
}

ActivePlaylist::ActivePlaylist(Playback::QueuePlayer& player, QObject* parent)
    : QObject(parent)
    , player_(player)
    , tracks_(player)
{
}

int ActivePlaylist::count() const { return tracks_.rowCount(); }

QString ActivePlaylist::title() const { return tr("Dev tracks"); }

void ActivePlaylist::play(int row) { player_.playIndex(row); }

} // namespace ViewModel
