#include "ActiveTracks.h"

#include "ActivePlaylist.h"
#include "PlaybackController.h"

namespace ViewModel {

ActiveTracks::ActiveTracks(ActivePlaylist& activePlaylist, Playback::PlaybackController& playback,
    Library::TrackStates& trackStates, Preload& preload, Covers::CoverArtCache& covers, QObject* parent)
    : TrackListModel(parent)
    , activePlaylist_(activePlaylist)
    , playback_(playback)
{
    setTrackStates(&trackStates);
    setPreload(&preload);
    setCovers(&covers);
    connect(&activePlaylist_, &ActivePlaylist::entriesChanged, this, &ActiveTracks::refill);
    connect(&playback_, &Playback::PlaybackController::trackChanged, this, &ActiveTracks::markCurrent);
    refill();
}

void ActiveTracks::refill()
{
    const QVector<Playback::QueueEntry> entries = activePlaylist_.entries();
    QList<MixedSourceEntry> rows;
    rows.reserve(entries.size());
    for (const Playback::QueueEntry& entry : entries)
        rows.append({ entry.sourceId, entry.track, QDateTime() });
    updateMixedSourceTracks(rows);
    // Past the rows that went: where the current one is may have moved, and no
    // reset says so to whoever shows it any more.
    markCurrent();
}

void ActiveTracks::markCurrent()
{
    const int previous = current_;
    current_ = playback_.currentIndex();
    for (const int row : { previous, current_ }) {
        if (row >= 0 && row < rowCount())
            emit dataChanged(index(row), index(row), { IsCurrentRole });
    }
}

QVariant ActiveTracks::data(const QModelIndex& index, int role) const
{
    if (role == IsCurrentRole)
        return index.row() == current_;
    return TrackListModel::data(index, role);
}

} // namespace ViewModel
