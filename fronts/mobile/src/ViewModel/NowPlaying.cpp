#include "NowPlaying.h"

#include <QUrl>

#include "QueuePlayer.h"

namespace ViewModel {

namespace {
// Fine enough for a seek bar, rare enough for the replica.
constexpr int kPositionIntervalMs = 250;
}

NowPlaying::NowPlaying(Playback::QueuePlayer& player, QObject* parent)
    : QObject(parent)
    , player_(player)
    , positionThrottle_(kPositionIntervalMs, [this] { emit positionChanged(); })
{
    connect(&player_, &Playback::QueuePlayer::currentChanged, this, &NowPlaying::trackChanged);
    connect(&player_, &Playback::QueuePlayer::playingChanged, this, &NowPlaying::playingChanged);
    connect(&player_, &Playback::QueuePlayer::loadingChanged, this, &NowPlaying::loadingChanged);
    connect(&player_, &Playback::QueuePlayer::positionChanged, &positionThrottle_, &Timing::Throttle::trigger);
}

bool NowPlaying::hasTrack() const { return player_.currentTrack() != nullptr; }

QString NowPlaying::title() const
{
    const Playback::Track* track = player_.currentTrack();
    return track ? track->title : QString();
}

QString NowPlaying::artist() const
{
    const Playback::Track* track = player_.currentTrack();
    return track ? track->artist : QString();
}

QString NowPlaying::coverUrl() const
{
    const Playback::Track* track = player_.currentTrack();
    return track && !track->cover.isEmpty() ? QUrl::fromLocalFile(track->cover).toString() : QString();
}

int NowPlaying::currentIndex() const { return player_.currentIndex(); }

bool NowPlaying::playing() const { return player_.playing(); }

bool NowPlaying::loading() const { return player_.loading(); }

qint64 NowPlaying::positionMs() const { return player_.positionMs(); }

qint64 NowPlaying::durationMs() const { return player_.durationMs(); }

void NowPlaying::play() { player_.play(); }

void NowPlaying::pause() { player_.pause(); }

void NowPlaying::togglePause() { player_.togglePause(); }

void NowPlaying::next() { player_.next(); }

void NowPlaying::previous() { player_.previous(); }

void NowPlaying::seek(qint64 positionMs) { player_.seek(positionMs); }

} // namespace ViewModel
