#include "MprisService.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QRegularExpression>

#include "PlaybackController.h"

namespace Integration {

MprisRootAdaptor::MprisRootAdaptor(QObject* host)
    : QDBusAbstractAdaptor(host)
{
}

void MprisRootAdaptor::Quit() { emit quitRequested(); }

void MprisRootAdaptor::Raise() { emit raiseRequested(); }

MprisPlayerAdaptor::MprisPlayerAdaptor(Playback::PlaybackController& playback, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , playback_(playback)
{
    connect(&playback_, &Playback::PlaybackController::trackChanged, this, &MprisPlayerAdaptor::onTrackChanged);
    connect(&playback_, &Playback::PlaybackController::playingChanged, this, &MprisPlayerAdaptor::onPlayingChanged);
    connect(&playback_, &Playback::PlaybackController::playModeChanged, this,
        [this]() { emitPropertiesChanged({ QStringLiteral("Shuffle"), QStringLiteral("LoopStatus") }); });
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    if (!playback_.hasCurrentTrack())
        return QStringLiteral("Stopped");
    return playback_.isPlaying() ? QStringLiteral("Playing") : QStringLiteral("Paused");
}

qlonglong MprisPlayerAdaptor::position() const
{
    return 0; // position is pushed via NowPlayingBar/positionChanged; MPRIS position polling is not wired up yet
}

void MprisPlayerAdaptor::setVolume(double v)
{
    volume_ = v;
    playback_.setVolume(static_cast<int>(v * 100));
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

void MprisPlayerAdaptor::Next() { playback_.next(); }

void MprisPlayerAdaptor::Previous() { playback_.previous(); }

void MprisPlayerAdaptor::Pause()
{
    if (playback_.isPlaying())
        playback_.togglePause();
}

void MprisPlayerAdaptor::PlayPause() { playback_.togglePause(); }

void MprisPlayerAdaptor::Play()
{
    if (!playback_.isPlaying())
        playback_.togglePause();
}

void MprisPlayerAdaptor::Stop()
{
    // No dedicated "stop" concept in PlaybackController yet; pausing is the
    // closest equivalent available without extending the playback API.
    if (playback_.isPlaying())
        playback_.togglePause();
}

void MprisPlayerAdaptor::Seek(qlonglong offsetUs)
{
    Q_UNUSED(offsetUs);
    // Relative seek isn't wired up (PlaybackController::seek is absolute,
    // and MPRIS position tracking isn't implemented yet — see position()).
}

void MprisPlayerAdaptor::onTrackChanged()
{
    if (!playback_.hasCurrentTrack())
        return;
    const Track& track = playback_.currentTrack();
    QVariantMap metadata;
    QString sanitizedId = track.id;
    sanitizedId.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")), QStringLiteral("_"));
    const QDBusObjectPath trackPath(QStringLiteral("/org/cloudmus/Track/%1").arg(sanitizedId));
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
    emitPropertiesChanged({ QStringLiteral("Metadata"), QStringLiteral("PlaybackStatus") });
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

MprisService::MprisService(Playback::PlaybackController& playback, QObject* parent)
    : QObject(parent)
{
    // Its own object on the bus, not the main window: that one can be
    // replaced (Ui::WindowHost) while this stays registered.
    auto* host = new QObject(this);
    root_ = new MprisRootAdaptor(host);
    player_ = new MprisPlayerAdaptor(playback, host);
    connect(root_, &MprisRootAdaptor::quitRequested, this, &MprisService::quitRequested);
    connect(root_, &MprisRootAdaptor::raiseRequested, this, &MprisService::raiseRequested);

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), host, QDBusConnection::ExportAdaptors);
    bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.cloudmus"));
}

} // namespace Integration
