#pragma once

#include "TrackListModel.h"

namespace Covers {
class CoverArtCache;
}
namespace Library {
class TrackStates;
}
namespace Playback {
class PlaybackController;
}

namespace ViewModel {

class ActivePlaylist;
class Preload;

// The active playlist's tracks as rows, kept up to date, with the one that
// plays marked: the main list of a view that has nothing else to add to it.
class ActiveTracks : public TrackListModel {
    Q_OBJECT

public:
    ActiveTracks(ActivePlaylist& activePlaylist, Playback::PlaybackController& playback,
        Library::TrackStates& trackStates, Preload& preload, Covers::CoverArtCache& covers, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    void refill();
    void markCurrent();

    ActivePlaylist& activePlaylist_;
    Playback::PlaybackController& playback_;
    int current_ = -1;
};

} // namespace ViewModel
