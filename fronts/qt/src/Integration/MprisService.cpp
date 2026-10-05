#include "MprisService.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>

#include <algorithm>
#include <cmath>

#include "NowPlaying.h"
#include "PlaybackController.h"

namespace Integration {

MprisRootAdaptor::MprisRootAdaptor(QObject* host)
    : QDBusAbstractAdaptor(host)
{
}

void MprisRootAdaptor::Quit() { emit quitRequested(); }

void MprisRootAdaptor::Raise() { emit raiseRequested(); }

MprisPlayerAdaptor::MprisPlayerAdaptor(
    Playback::PlaybackController& playback, ViewModel::NowPlaying& nowPlaying, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , playback_(playback)
    , nowPlaying_(nowPlaying)
{
    connect(&playback_, &Playback::PlaybackController::trackChanged, this, &MprisPlayerAdaptor::onTrackChanged);
    connect(&playback_, &Playback::PlaybackController::playingChanged, this, &MprisPlayerAdaptor::onPlayingChanged);
    connect(&playback_, &Playback::PlaybackController::playModeChanged, this,
        [this]() { emitPropertiesChanged({ QStringLiteral("Shuffle"), QStringLiteral("LoopStatus") }); });
    connect(&playback_, &Playback::PlaybackController::seeked, this,
        [this](qint64 positionMs) { emit Seeked(positionMs * 1000); });
    connect(&playback_, &Playback::PlaybackController::currentTrackAvailabilityChanged, this, [this](bool available) {
        if (!available)
            onTrackChanged();
    });
    connect(&nowPlaying_, &ViewModel::NowPlaying::volumeChanged, this,
        [this](int) { emitPropertiesChanged({ QStringLiteral("Volume") }); });
    if (playback_.hasCurrentTrack())
        onTrackChanged();
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    if (!playback_.hasCurrentTrack())
        return QStringLiteral("Stopped");
    return playback_.isPlaying() ? QStringLiteral("Playing") : QStringLiteral("Paused");
}

qlonglong MprisPlayerAdaptor::position() const { return playback_.positionMs() * 1000; }

double MprisPlayerAdaptor::volume() const { return nowPlaying_.volume() / 100.0; }

void MprisPlayerAdaptor::setVolume(double v)
{
    if (!std::isfinite(v))
        return;
    nowPlaying_.setVolume(static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 100)));
}

bool MprisPlayerAdaptor::canSeek() const
{
    return playback_.hasCurrentTrack() && playback_.currentTrack().durationMs > 0;
}

bool MprisPlayerAdaptor::shuffle() const { return playback_.shuffleActive(); }

void MprisPlayerAdaptor::setShuffle(bool on) { playback_.setShuffle(on); }

QString MprisPlayerAdaptor::loopStatus() const
{
    switch (playback_.effectiveRepeatMode()) {
        case Playback::RepeatMode::All:
            return QStringLiteral("Playlist");
        case Playback::RepeatMode::One:
            return QStringLiteral("Track");
        case Playback::RepeatMode::Off:
            break;
    }
    return QStringLiteral("None");
}

void MprisPlayerAdaptor::setLoopStatus(const QString& status)
{
    if (status == QStringLiteral("Playlist"))
        playback_.setRepeatMode(Playback::RepeatMode::All);
    else if (status == QStringLiteral("Track"))
        playback_.setRepeatMode(Playback::RepeatMode::One);
    else if (status == QStringLiteral("None"))
        playback_.setRepeatMode(Playback::RepeatMode::Off);
}

void MprisPlayerAdaptor::Next()
{
    onCommand();
    playback_.next();
}

void MprisPlayerAdaptor::Previous()
{
    onCommand();
    playback_.previous();
}

void MprisPlayerAdaptor::Pause()
{
    onCommand();
    if (playback_.isPlaying())
        playback_.togglePause();
}

void MprisPlayerAdaptor::PlayPause()
{
    onCommand();
    playback_.togglePause();
}

void MprisPlayerAdaptor::Play()
{
    onCommand();
    if (!playback_.isPlaying())
        playback_.togglePause();
}

void MprisPlayerAdaptor::Stop()
{
    onCommand();
    // No dedicated "stop" concept in PlaybackController yet; pausing is the
    // closest equivalent available without extending the playback API.
    if (playback_.isPlaying())
        playback_.togglePause();
}

void MprisPlayerAdaptor::Seek(qlonglong offsetUs)
{
    if (!canSeek())
        return;
    const qint64 positionMs = playback_.positionMs();
    const qint64 durationMs = playback_.currentTrack().durationMs;
    const qint64 offsetMs = offsetUs / 1000;
    if (offsetMs > 0 && offsetMs >= durationMs - positionMs) {
        playback_.next();
    } else {
        playback_.seek(offsetMs < -positionMs ? 0 : positionMs + offsetMs);
    }
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath& trackId, qlonglong positionUs)
{
    if (!canSeek() || trackId.path() != metadata_.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>().path()
        || positionUs < 0 || positionUs > qint64(playback_.currentTrack().durationMs) * 1000)
        return;
    playback_.seek(positionUs / 1000);
}

void MprisPlayerAdaptor::onTrackChanged()
{
    if (!playback_.hasCurrentTrack()) {
        metadata_.clear();
        emitPropertiesChanged(
            { QStringLiteral("Metadata"), QStringLiteral("PlaybackStatus"), QStringLiteral("CanSeek") });
        return;
    }
    const Track& track = playback_.currentTrack();
    QVariantMap metadata;
    const QDBusObjectPath trackPath(QStringLiteral("/org/cloudmus/Track/%1").arg(++trackSerial_));
    metadata.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(trackPath));
    metadata.insert(QStringLiteral("mpris:length"), static_cast<qlonglong>(track.durationMs) * 1000);
    metadata.insert(QStringLiteral("xesam:title"), track.title);
    QStringList artists;
    for (const Artist& a : track.artists)
        artists.append(a.name);
    metadata.insert(QStringLiteral("xesam:artist"), artists);
    if (track.coverUrl)
        metadata.insert(QStringLiteral("mpris:artUrl"), *track.coverUrl);
    metadata_ = metadata;
    emitPropertiesChanged({ QStringLiteral("Metadata"), QStringLiteral("PlaybackStatus"), QStringLiteral("CanSeek") });
}

void MprisPlayerAdaptor::onPlayingChanged(bool) { emitPropertiesChanged({ QStringLiteral("PlaybackStatus") }); }

void MprisPlayerAdaptor::emitPropertiesChanged(const QStringList& properties)
{
    QVariantMap changed;
    for (const QString& name : properties) {
        changed.insert(name, property(name.toUtf8().constData()));
    }
    QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
    signal << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

MprisService::MprisService(Playback::PlaybackController& playback, ViewModel::NowPlaying& nowPlaying, QObject* parent)
    : QObject(parent)
{
    // Its own object on the bus, not the main window: that one can be
    // replaced (Ui::WindowHost) while this stays registered.
    auto* host = new QObject(this);
    root_ = new MprisRootAdaptor(host);
    player_ = new MprisPlayerAdaptor(playback, nowPlaying, host);
    player_->onCommand = [this]() { emit commandReceived(); };
    connect(root_, &MprisRootAdaptor::quitRequested, this, &MprisService::quitRequested);
    connect(root_, &MprisRootAdaptor::raiseRequested, this, &MprisService::raiseRequested);

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), host, QDBusConnection::ExportAdaptors);
    bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.cloudmus"));
}

} // namespace Integration
