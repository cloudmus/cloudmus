#include "AudioPlayer.h"

#include <clocale>
#include <cstring>

#include <QByteArray>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

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

    // Lets the AppImage build point mpv's ytdl_hook script at its own
    // bundled yt-dlp, without which every stream URL that hook doesn't
    // immediately recognize fails ("youtube-dl failed: not found or not
    // enough permissions", then a much less obvious "unrecognized file
    // format" from mpv's own top-level error) — see
    // packaging/appimage/AppRun's own comments for why the script-opts
    // route mpv 0.32.0 (Debian 11's libmpv, what the AppImage bundles)
    // doesn't support at all, and why an isolated MPV_HOME wasn't
    // sufficient on its own either: embedded libmpv defaults "config" to
    // "no" specifically to avoid touching *any* user files unless asked,
    // which turns out to also gate mp.find_config_file() in
    // ytdl_hook.lua, not just mpv.conf/input.conf loading — confirmed by
    // this still failing with only MPV_HOME set. Setting the config
    // directory through mpv's own C API instead of trusting it to notice
    // the env var is deliberate, not just belt-and-suspenders: it's the
    // one mechanism actually confirmed to work. Harmless when unset
    // (plain dev/system runs): "config" stays at its default "no".
    const QByteArray mpvConfigDir = qgetenv("CLOUDMUS_MPV_CONFIG_DIR");
    if (!mpvConfigDir.isEmpty()) {
        mpv_set_option_string(mpv_, "config-dir", mpvConfigDir.constData());
        mpv_set_option_string(mpv_, "config", "yes");
    }

    // libavformat (mpv's network/demuxer layer) links against GnuTLS, not
    // the OpenSSL bundled above for Qt's own TLS backend — a completely
    // separate stack, compiled by Debian with Debian's own default CA
    // trust-store path (/etc/ssl/certs/ca-certificates.crt) baked in.
    // That path doesn't exist on every distro (e.g. openSUSE uses
    // per-certificate hashed symlinks under /etc/ssl/certs instead of one
    // combined bundle file there) — when it's missing, TLS certificate
    // verification fails silently rather than with a clear error: mpv's
    // own demuxer probe just reports an empty Mime-type and "No format
    // found" for what should be a perfectly normal audio stream URL, and
    // even mpv's ytdl_hook fallback (yt-dlp itself uses Python's own,
    // unaffected TLS stack, and succeeds) can't work around it, since the
    // URL yt-dlp hands back still has to go through this same
    // GnuTLS-backed fetch to actually play. CLOUDMUS_TLS_CA_FILE points
    // at the certifi CA bundle already bundled as a backend dependency
    // (requests/urllib3 pull it in) — reusing it here instead of
    // shipping a second copy of the same certificate list.
    const QByteArray tlsCaFile = qgetenv("CLOUDMUS_TLS_CA_FILE");
    if (!tlsCaFile.isEmpty()) {
        mpv_set_option_string(mpv_, "tls-ca-file", tlsCaFile.constData());
    }

    if (mpv_initialize(mpv_) < 0) {
        qCWarning(lcAudioPlayer) << "mpv_initialize failed";
    }

    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_set_wakeup_callback(mpv_, &AudioPlayer::mpvWakeup, this);

    // mpv's own internal log (network/demuxer/protocol errors — much more
    // specific than the generic mpv_error_string() surfaced from
    // MPV_END_FILE_REASON_ERROR, e.g. "unrecognized file format" gives no
    // hint of *why* on its own) forwarded through the same logging
    // category, warn/error at qCWarning, everything else at qCDebug.
    mpv_request_log_messages(mpv_, "info");

    networkManager_ = new QNetworkAccessManager(this);
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

    case MPV_EVENT_LOG_MESSAGE: {
        const auto* msg = static_cast<mpv_event_log_message*>(event.data);
        const QString text = QString::fromUtf8(msg->text).trimmed();
        if (text.isEmpty())
            break;
        if (std::strcmp(msg->level, "error") == 0 || std::strcmp(msg->level, "warn") == 0)
            qCWarning(lcAudioPlayer) << "[mpv]" << text;
        else
            qCDebug(lcAudioPlayer) << "[mpv]" << text;
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

    // A HEAD preflight through Qt's own network stack — which follows
    // HTTP redirects (including 308) automatically by default — to
    // resolve the *actual* playable URL before handing mpv anything.
    // Some backends' stream URLs (confirmed: Yandex Music's get-mp3
    // endpoint) are themselves a bare HTTP 308 pointing at the real
    // stream host, not the audio directly, and mpv's bundled libavformat
    // (Debian 11's ffmpeg 4.3.7, in the AppImage build specifically)
    // doesn't reliably follow it — confirmed by `curl` needing an
    // explicit -L for the exact same URL, and by mpv's own log reporting
    // an empty Mime-type and "No format found" against the *unresolved*
    // redirect response otherwise (both with and without its ytdl_hook
    // fallback also in play — yt-dlp's own generic extractor just hands
    // the same unresolved URL straight back for a direct-file link like
    // this one, rather than actually resolving the redirect itself).
    //
    // A fresh play() call aborts any reply still in flight from an
    // already-superseded track first, so a stale resolution can never
    // race ahead of a newer one and load the wrong file.
    if (pendingRedirectResolve_) {
        pendingRedirectResolve_->disconnect(this);
        pendingRedirectResolve_->abort();
        pendingRedirectResolve_->deleteLater();
        pendingRedirectResolve_ = nullptr;
    }

    QNetworkReply* reply = networkManager_->head(QNetworkRequest(QUrl(url)));
    pendingRedirectResolve_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        if (pendingRedirectResolve_ != reply) {
            // Already superseded by a newer play() call, which owns
            // cleaning up its own (different) reply — nothing to do.
            return;
        }
        pendingRedirectResolve_ = nullptr;
        // Fall back to the original URL on any preflight failure (e.g. a
        // server that doesn't support HEAD) rather than blocking
        // playback entirely on what both is, and should stay, a
        // best-effort nicety.
        const QUrl resolvedUrl = reply->error() == QNetworkReply::NoError ? reply->url() : QUrl(url);
        reply->deleteLater();
        loadUrl(resolvedUrl.toString());
    });
}

void AudioPlayer::loadUrl(const QString& url)
{
    // Fed straight to mpv, no local download-and-buffer step — unlike the
    // old QMediaPlayer wrapper, whose FFmpeg-based HTTP client had its
    // connection reset mid-track by the CDN. fronts/tui's playback_engine.py
    // hands mpv the raw stream URL directly against the same CDN without
    // that problem, so this follows suit.
    qCDebug(lcAudioPlayer) << "loading URL:" << url;
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
