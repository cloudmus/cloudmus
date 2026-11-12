#include "QueuePlayer.h"

#include <QLoggingCategory>

#include "AudioPlayer.h"

namespace Playback {

namespace {
Q_LOGGING_CATEGORY(lcQueue, "cloudmus.playback.queue")

constexpr qint64 kRestartThresholdMs = 3000;
constexpr int kSaveIntervalMs = 5000;
const QString kIndexKey = QStringLiteral("playback/index");
const QString kTrackKey = QStringLiteral("playback/trackId");
const QString kPositionKey = QStringLiteral("playback/positionMs");
}

QueuePlayer::QueuePlayer(QList<Track> tracks, QSettings& state, QObject* parent)
    : QObject(parent)
    , tracks_(std::move(tracks))
    , state_(state)
    , audio_(new AudioPlayer(this))
{
    if (!tracks_.isEmpty()) {
        // The saved track by id first: the list may have changed.
        index_ = 0;
        const QString savedId = state_.value(kTrackKey).toString();
        for (int i = 0; i < tracks_.size(); ++i) {
            if (tracks_[i].id == savedId) {
                index_ = i;
                positionMs_ = state_.value(kPositionKey).toLongLong();
                break;
            }
        }
        pendingSeekMs_ = positionMs_;
        qCInfo(lcQueue) << "restored track" << index_ << "at" << positionMs_ << "ms";
    }

    connect(audio_, &AudioPlayer::started, this, [this] {
        if (pendingSeekMs_ > 0) {
            audio_->seek(pendingSeekMs_);
            pendingSeekMs_ = 0;
        }
        setLoading(false);
        if (!heardAudio_) {
            heardAudio_ = true;
            emit firstAudio();
        }
    });
    connect(audio_, &AudioPlayer::positionChanged, this, [this](qint64 positionMs, qint64 durationMs) {
        // mpv reports 0 before the pending seek lands: keep the saved spot.
        if (pendingSeekMs_ > 0)
            return;
        positionMs_ = positionMs;
        durationMs_ = durationMs;
        emit positionChanged();
    });
    connect(audio_, &AudioPlayer::endOfFile, this, [this] {
        qCInfo(lcQueue) << "track ended:" << index_;
        // Round and round: the dev list is short, and the car should keep
        // playing.
        start((index_ + 1) % int(tracks_.size()), 0);
    });
    connect(audio_, &AudioPlayer::failed, this, [this](const QString& message) {
        qCWarning(lcQueue) << "track" << index_ << "failed:" << message;
        loaded_ = false;
        setLoading(false);
        setPlaying(false);
    });

    saveTimer_.setInterval(kSaveIntervalMs);
    connect(&saveTimer_, &QTimer::timeout, this, &QueuePlayer::saveState);
}

QueuePlayer::~QueuePlayer() { saveState(); }

const Track* QueuePlayer::currentTrack() const
{
    return index_ >= 0 && index_ < tracks_.size() ? &tracks_[index_] : nullptr;
}

qint64 QueuePlayer::durationMs() const
{
    if (durationMs_ > 0)
        return durationMs_;
    const Track* track = currentTrack();
    return track ? track->durationMs : 0;
}

void QueuePlayer::play()
{
    if (index_ < 0)
        return;
    if (!loaded_) {
        start(index_, positionMs_);
        return;
    }
    audio_->resume();
    setPlaying(true);
}

void QueuePlayer::pause()
{
    if (!playing_)
        return;
    audio_->pause();
    setPlaying(false);
    saveState();
}

void QueuePlayer::togglePause() { playing_ ? pause() : play(); }

void QueuePlayer::next()
{
    if (!tracks_.isEmpty())
        start((index_ + 1) % int(tracks_.size()), 0);
}

void QueuePlayer::previous()
{
    if (tracks_.isEmpty())
        return;
    if (positionMs_ > kRestartThresholdMs)
        start(index_, 0);
    else
        start((index_ - 1 + int(tracks_.size())) % int(tracks_.size()), 0);
}

void QueuePlayer::playIndex(int index)
{
    if (index >= 0 && index < tracks_.size())
        start(index, 0);
}

void QueuePlayer::seek(qint64 positionMs)
{
    positionMs_ = positionMs;
    if (loaded_)
        audio_->seek(positionMs);
    else
        pendingSeekMs_ = positionMs;
    emit positionChanged();
}

void QueuePlayer::start(int index, qint64 fromMs)
{
    const bool changed = index != index_;
    index_ = index;
    const Track& track = tracks_[index];
    qCInfo(lcQueue) << "playing" << index << track.artist << "-" << track.title << "from" << fromMs << "ms";
    positionMs_ = fromMs;
    pendingSeekMs_ = fromMs;
    durationMs_ = 0;
    loaded_ = true;
    audio_->play(track.file, track.artist + QStringLiteral(" - ") + track.title);
    setLoading(true);
    setPlaying(true);
    if (changed)
        emit currentChanged();
    emit positionChanged();
    saveState();
}

void QueuePlayer::setPlaying(bool playing)
{
    if (playing_ == playing)
        return;
    playing_ = playing;
    if (playing)
        saveTimer_.start();
    else
        saveTimer_.stop();
    emit playingChanged();
}

void QueuePlayer::setLoading(bool loading)
{
    if (loading_ == loading)
        return;
    loading_ = loading;
    emit loadingChanged();
}

void QueuePlayer::saveState()
{
    const Track* track = currentTrack();
    if (!track)
        return;
    state_.setValue(kTrackKey, track->id);
    state_.setValue(kIndexKey, index_);
    state_.setValue(kPositionKey, positionMs_);
    state_.sync();
}

} // namespace Playback
