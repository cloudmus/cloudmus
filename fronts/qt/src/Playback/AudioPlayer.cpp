#include "AudioPlayer.h"

#include <clocale>
#include <cstring>

#include <QByteArray>
#include <QLoggingCategory>
#include <QMetaObject>

#include <mpv/client.h>

namespace Playback {

namespace {
Q_LOGGING_CATEGORY(lcAudioPlayer, "cloudmus.playback.audio")
}

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject(parent)
{
    // libmpv requires LC_NUMERIC == "C" before mpv_create() — it parses/
    // formats numeric option values with the C locale's decimal point, and
    // silently misbehaves (crashes, in practice) if Qt or the environment
    // has switched it to something else (a comma-decimal locale here).
    std::setlocale(LC_NUMERIC, "C");

    mpv_ = mpv_create();

    // Audio only: no video output means no GPU context ever gets created
    // for it (see git log for why that matters on hybrid Intel+NVIDIA
    // laptops). ao=pulse,alsa mirrors fronts/tui/playback_engine.py — mpv's
    // native PipeWire output was unreliable there; route through
    // pulse/pipewire-pulse or alsa instead.
    mpv_set_option_string(mpv_, "vid", "no");
    mpv_set_option_string(mpv_, "ao", "pulse,alsa");

    // Without these, mpv reports itself to PipeWire/Pulse (and thus to the
    // desktop's per-stream volume widget) as "mpv" playing a title derived
    // from the raw stream URL, with mpv's default "${media-title} - mpv"
    // title template tacking " - mpv" onto the end.
    mpv_set_option_string(mpv_, "audio-client-name", "CloudMus");
    mpv_set_option_string(mpv_, "title", "${media-title}");

    if (mpv_initialize(mpv_) < 0) {
        qCWarning(lcAudioPlayer) << "mpv_initialize failed";
    }

    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_set_wakeup_callback(mpv_, &AudioPlayer::mpvWakeup, this);
}

AudioPlayer::~AudioPlayer()
{
    mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    mpv_terminate_destroy(mpv_);
}

void AudioPlayer::mpvWakeup(void* ctx)
{
    // Called from one of libmpv's internal threads — never touch mpv_ here,
    // just hop to the GUI thread where processMpvEvents() actually drains
    // the queue.
    QMetaObject::invokeMethod(static_cast<AudioPlayer*>(ctx), "processMpvEvents", Qt::QueuedConnection);
}

void AudioPlayer::processMpvEvents()
{
    for (;;) {
        mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(*event);
    }
}

void AudioPlayer::handleEvent(const mpv_event& event)
{
    switch (event.event_id) {
    case MPV_EVENT_PLAYBACK_RESTART:
        emit started();
        break;

    case MPV_EVENT_END_FILE: {
        const auto* data = static_cast<mpv_event_end_file*>(event.data);
        if (data->reason == MPV_END_FILE_REASON_EOF) {
            emit endOfFile();
        } else if (data->reason == MPV_END_FILE_REASON_ERROR) {
            const QString message = QString::fromUtf8(mpv_error_string(data->error));
            qCWarning(lcAudioPlayer) << "playback failed:" << message;
            emit failed(message);
        }
        // STOP/QUIT/REDIRECT are our own doing (stop()/next loadfile) — no
        // signal, same as the old QMediaPlayer wrapper's stop() not firing
        // endOfFile().
        break;
    }

    case MPV_EVENT_PROPERTY_CHANGE: {
        const auto* prop = static_cast<mpv_event_property*>(event.data);
        if (prop->format != MPV_FORMAT_DOUBLE)
            break;
        const qint64 ms = static_cast<qint64>(*static_cast<double*>(prop->data) * 1000.0);
        if (std::strcmp(prop->name, "time-pos") == 0)
            lastPositionMs_ = ms;
        else if (std::strcmp(prop->name, "duration") == 0)
            lastDurationMs_ = ms;
        else
            break;
        emit positionChanged(lastPositionMs_, lastDurationMs_);
        break;
    }

    default:
        break;
    }
}

void AudioPlayer::play(const QString& url, const QString& title)
{
    // Set before loadfile, not after, so the incoming file picks it up
    // immediately instead of racing mpv's own URL-derived fallback title.
    const QByteArray titleUtf8 = title.toUtf8();
    mpv_set_property_string(mpv_, "force-media-title", titleUtf8.constData());

    // Fed straight to mpv, no local download-and-buffer step — unlike the
    // old QMediaPlayer wrapper, whose FFmpeg-based HTTP client had its
    // connection reset mid-track by the CDN. fronts/tui's playback_engine.py
    // hands mpv the raw stream URL directly against the same CDN without
    // that problem, so this follows suit.
    const QByteArray urlUtf8 = url.toUtf8();
    const char* args[] = { "loadfile", urlUtf8.constData(), "replace", nullptr };
    mpv_command_async(mpv_, 0, args);
}

void AudioPlayer::pause()
{
    int flag = 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &flag);
}

void AudioPlayer::resume()
{
    int flag = 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &flag);
}

void AudioPlayer::stop()
{
    const char* args[] = { "stop", nullptr };
    mpv_command_async(mpv_, 0, args);
}

void AudioPlayer::seek(qint64 positionMs)
{
    const QByteArray posSeconds = QByteArray::number(positionMs / 1000.0, 'f', 3);
    const char* args[] = { "seek", posSeconds.constData(), "absolute", nullptr };
    mpv_command_async(mpv_, 0, args);
}

void AudioPlayer::setVolume(int volume0To100)
{
    int64_t vol = volume0To100;
    mpv_set_property(mpv_, "volume", MPV_FORMAT_INT64, &vol);
}

} // namespace Playback
