#include "AudioPlayer.h"

#include <clocale>
#include <cmath>
#include <cstring>
#include <numbers>
#include <utility>

#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkProxy>
#include <QTimer>
#include <QUrl>

#include <mpv/client.h>

#include "StreamRelay.h"

namespace Playback {

namespace {
Q_LOGGING_CATEGORY(lcAudioPlayer, "cloudmus.playback.audio")

// A loudness analyzer on a side branch: what's played goes through `anull`
// untouched (checked bit-exact), a copy is mixed to mono, split into the
// full band (channel 1) and the bass below 150 Hz (channel 2), cut into
// 1024-sample frames (~23 ms) and measured; ametadata prints each frame's
// timestamp and levels to mpv's log, where LevelFeed picks them up (see
// takeLevels()). Only options ffmpeg 4.3 already has — what the AppImage's
// Debian 11 libmpv links. "@cmlevel" labels it for `af remove`.
constexpr char kLevelFilterLabel[] = "@cmlevel";
constexpr char kLevelFilter[] = "@cmlevel:lavfi=[asplit[play][an];"
                                "[an]pan=mono|c0=0.5*c0+0.5*c1,asplit[full][low];"
                                "[low]lowpass=f=150[bass];"
                                "[full][bass]join=inputs=2:channel_layout=stereo,asetnsamples=n=1024:p=0,"
                                "astats=metadata=1:reset=1,"
                                "ametadata=mode=print:key=lavfi.astats.1.RMS_level,"
                                "ametadata=mode=print:key=lavfi.astats.2.RMS_level,anullsink;"
                                "[play]anull]";

// The fade's floor: mpv's volume-gain goes no lower by default, and it's
// below anything audible.
constexpr double kSilentDb = -96.0;
// A step of the fade: finer than an OpenSL chunk of 256 frames, so each
// chunk the output takes has a level of its own.
constexpr int kFadeTickMs = 4;
}

AudioPlayer::AudioPlayer(QObject* parent, const QByteArray& audioOutput)
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
    // laptops). Native PipeWire first: through pipewire-pulse the same
    // playback costs ~3x the CPU (7.8% vs 2.6% of a core, measured on
    // libmpv 0.41 with a Bluetooth sink) — libpulse wakes ~400 times a
    // second. It was dropped once (commit 322cd07): a libpipewire bundled
    // with the app and a system server of another version break locking
    // inside pw_stream_*. A build against the system's libmpv is safe; a
    // bundle isn't, so the AppImage keeps to pulse/alsa — its libmpv 0.32
    // has no pipewire output yet, this guards a future base image. A libmpv
    // without pipewire skips the unknown entry anyway.
    QByteArray output = audioOutput;
    if (qEnvironmentVariableIsSet("APPIMAGE") && output.startsWith("pipewire,"))
        output.remove(0, int(sizeof("pipewire,")) - 1);
    mpv_set_option_string(mpv_, "vid", "no");
    mpv_set_option_string(mpv_, "ao", output.constData());
    mpv_set_option_string(mpv_, "gapless-audio", "yes");
    // Without these, mpv reports itself to PipeWire/Pulse (and thus to the
    // desktop's per-stream volume widget) as "mpv" playing a title derived
    // from the raw stream URL, with mpv's default "${media-title} - mpv"
    // title template tacking " - mpv" onto the end.
    mpv_set_option_string(mpv_, "audio-client-name", "CloudMus");
    mpv_set_option_string(mpv_, "title", "${media-title}");

#ifdef Q_OS_WIN
    // The packaged helper passes mpv's yt-dlp request to our bundled Python.
    // Current Windows libmpv supports this hook option directly.
    const QString ytDlpPath = QCoreApplication::applicationDirPath() + QStringLiteral("/yt-dlp.exe");
    if (QFileInfo::exists(ytDlpPath)) {
        const QByteArray option = "ytdl_hook-ytdl_path=" + ytDlpPath.toUtf8();
        mpv_set_option_string(mpv_, "script-opts", option.constData());
    }
#endif

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
    QByteArray tlsCaFile = qgetenv("CLOUDMUS_TLS_CA_FILE");
#ifdef Q_OS_WIN
    if (tlsCaFile.isEmpty()) {
        const QString certifi
            = QCoreApplication::applicationDirPath() + QStringLiteral("/python/Lib/site-packages/certifi/cacert.pem");
        if (QFileInfo::exists(certifi))
            tlsCaFile = certifi.toUtf8();
    }
#endif
    if (!tlsCaFile.isEmpty()) {
        mpv_set_option_string(mpv_, "tls-ca-file", tlsCaFile.constData());
    }

    if (mpv_initialize(mpv_) < 0) {
        qCWarning(lcAudioPlayer) << "mpv_initialize failed";
    }

    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    // How far into the track the demuxer cache reaches; unavailable (NONE)
    // without a cache, e.g. for a local file.
    mpv_observe_property(mpv_, 0, "demuxer-cache-time", MPV_FORMAT_DOUBLE);
    mpv_set_wakeup_callback(mpv_, &AudioPlayer::mpvWakeup, this);

    fadeTimer_ = new QTimer(this);
    fadeTimer_->setTimerType(Qt::PreciseTimer);
    fadeTimer_->setInterval(kFadeTickMs);
    connect(fadeTimer_, &QTimer::timeout, this, &AudioPlayer::onFadeTick);
    settleTimer_ = new QTimer(this);
    settleTimer_->setSingleShot(true);
    settleTimer_->setTimerType(Qt::PreciseTimer);
    connect(settleTimer_, &QTimer::timeout, this, &AudioPlayer::runQueued);

    // mpv's own internal log (network/demuxer/protocol errors — much more
    // specific than the generic mpv_error_string() surfaced from
    // MPV_END_FILE_REASON_ERROR, e.g. "unrecognized file format" gives no
    // hint of *why* on its own) forwarded through the same logging
    // category, warn/error at qCWarning, everything else at qCDebug.
    mpv_request_log_messages(mpv_, "info");
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
        case MPV_EVENT_START_FILE:
            if (preparedQueued_) {
                const QByteArray title = preparedTitle_.toUtf8();
                mpv_set_property_string(mpv_, "force-media-title", title.constData());
            }
            break;

        case MPV_EVENT_FILE_LOADED:
            if (preparedPromoting_) {
                const char* args[] = { "playlist-clear", nullptr };
                mpv_command_async(mpv_, 0, args);
                preparedPromoting_ = false;
            }
            break;

        case MPV_EVENT_PLAYBACK_RESTART:
            // It has taken: not for what's queued behind it.
            if (startPositionSet_) {
                startPositionSet_ = false;
                mpv_set_property_string(mpv_, "start", "none");
            }
            awaitingRestart_ = false;
            if (fadeMs_ > 0 && !paused_ && !deferring_)
                fadeTo(1.0);
            emit started();
            break;

        case MPV_EVENT_END_FILE: {
            const auto* data = static_cast<mpv_event_end_file*>(event.data);
            if (data->reason == MPV_END_FILE_REASON_EOF) {
                // Nothing heard until the next file (queued or not) restarts:
                // a change now has no sound to fade out.
                awaitingRestart_ = true;
                emit endOfFile();
            } else if (data->reason == MPV_END_FILE_REASON_ERROR) {
                awaitingRestart_ = true;
                const QString message = QString::fromUtf8(mpv_error_string(data->error));
                qCWarning(lcAudioPlayer) << "playback failed:" << message;
                Failure failure = Failure::Stream;
                if (data->error == MPV_ERROR_LOADING_FAILED || data->error == MPV_ERROR_UNKNOWN_FORMAT)
                    failure = Failure::File;
                else if (data->error == MPV_ERROR_AO_INIT_FAILED)
                    failure = Failure::Output;
                emit failed(message, failure);
            }
            // STOP/QUIT/REDIRECT are our own doing (stop()/next loadfile) — no
            // signal, same as the old QMediaPlayer wrapper's stop() not firing
            // endOfFile().
            break;
        }

        case MPV_EVENT_LOG_MESSAGE: {
            const auto* msg = static_cast<mpv_event_log_message*>(event.data);
            if (std::strcmp(msg->level, "v") == 0) {
                // Asked for only for the analyzer's lines (setLevelsEnabled());
                // the rest of mpv's verbose chatter stays out of our log.
                if (std::strcmp(msg->prefix, "ffmpeg") == 0)
                    levelFeed_.addLogLine(QByteArrayView(msg->text));
                break;
            }
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
            if (std::strcmp(prop->name, "demuxer-cache-time") == 0) {
                const qint64 ms = prop->format == MPV_FORMAT_DOUBLE
                    ? static_cast<qint64>(*static_cast<double*>(prop->data) * 1000.0)
                    : -1;
                // It moves with every packet read; a quarter second is finer
                // than a slider pixel anyway.
                if ((ms < 0) != (lastBufferedMs_ < 0) || qAbs(ms - lastBufferedMs_) >= 250) {
                    lastBufferedMs_ = ms;
                    emit bufferedChanged(ms);
                }
                break;
            }
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

void AudioPlayer::setStartPosition(qint64 positionMs)
{
    // A property rather than an option of loadfile, whose form differs between
    // the versions of mpv this runs on.
    setProperty("start", QByteArray::number(positionMs / 1000.0, 'f', 3));
    startPositionSet_ = true;
}

void AudioPlayer::play(const QString& url, const QString& title, const std::optional<QNetworkProxy>& route,
    const QMap<QString, QString>& headers)
{
    fadeOutFirst();
    clearPrepared();
    // Set before loadfile, not after, so the incoming file picks it up
    // immediately instead of racing mpv's own URL-derived fallback title.
    setProperty("force-media-title", title.toUtf8());

    // A file on disk has no connection to route, and the relay can't serve
    // it (Qt's file reply has no HTTP status) — whatever route or headers
    // the source carries.
    const QUrl parsed(url);
    if (parsed.isLocalFile() || parsed.scheme().isEmpty()) {
        loadUrl(url);
        return;
    }

    // Anything remote plays from the relay, mpv only ever reads plain HTTP
    // from loopback. The relay fetches through the source's proxy (or
    // explicitly directly — mpv itself would take http_proxy from the
    // environment), follows redirects with Qt's network stack — some
    // stream URLs (Yandex Music's get-mp3) are a bare 308 the AppImage's
    // libavformat doesn't follow — and resumes a stream that breaks off.
    // On Windows it is also what keeps libmpv alive: it crashed the whole
    // app when a track change cut off an HTTPS stream it was downloading
    // itself.
    loadUrl(relay()->urlFor(parsed, route.value_or(QNetworkProxy(QNetworkProxy::DefaultProxy)), headers).toString());
}

void AudioPlayer::play(StreamDownload* download, const QString& title)
{
    fadeOutFirst();
    clearPrepared();
    setProperty("force-media-title", title.toUtf8());
    loadUrl(relay()->urlFor(download).toString());
}

void AudioPlayer::prepare(const QString& file, const QString& title)
{
    clearPrepared();
    preparedTitle_ = title;
    command({ "loadfile", file.toUtf8(), "append" });
    preparedQueued_ = true;
    emit prepared();
}

StreamRelay* AudioPlayer::relay()
{
    if (relay_ == nullptr)
        relay_ = new StreamRelay(this);
    return relay_;
}

void AudioPlayer::clearPrepared()
{
    if (preparedQueued_ || preparedPromoting_)
        command({ "playlist-clear" });
    preparedQueued_ = false;
    preparedPromoting_ = false;
    preparedTitle_.clear();
}

void AudioPlayer::usePrepared(bool manual)
{
    if (manual)
        fadeOutFirst();
    setProperty("force-media-title", preparedTitle_.toUtf8());
    if (manual) {
        stopped_ = false;
        startSilent();
        command({ "playlist-next" });
        paused_ = false;
        setPaused(false);
    }
    preparedQueued_ = false;
    preparedPromoting_ = true;
}

void AudioPlayer::loadUrl(const QString& url)
{
    qCDebug(lcAudioPlayer) << "loading URL:" << url;
    fadeOutFirst();
    levelFeed_.clear();

    // mpv's "pause" property isn't reset by loadfile, so a new track must
    // clear it to always start audible regardless of whatever pause state
    // was left over from before.
    paused_ = false;
    stopped_ = false;
    setPaused(false);
    startSilent();
    command({ "loadfile", url.toUtf8(), "replace" });
}

void AudioPlayer::pause()
{
    fadeOutFirst();
    paused_ = true;
    setPaused(true);
}

void AudioPlayer::resume()
{
    paused_ = false;
    setPaused(false);
    // While a fade out is under way runQueued() brings it back up; after a
    // load or a seek, PLAYBACK_RESTART does.
    if (fadeMs_ > 0 && !deferring_ && !stopped_ && !awaitingRestart_)
        fadeTo(1.0);
}

void AudioPlayer::stop()
{
    fadeOutFirst();
    stopped_ = true;
    clearPrepared();
    command({ "stop" });
    levelFeed_.clear();
}

void AudioPlayer::seek(qint64 positionMs)
{
    fadeOutFirst();
    startSilent();
    command({ "seek", QByteArray::number(positionMs / 1000.0, 'f', 3), "absolute" });
    levelFeed_.clear();
}

void AudioPlayer::setEdges(int fadeMs, int settleMs)
{
    fadeMs_ = qMax(0, fadeMs);
    settleMs_ = qMax(0, settleMs);
    if (fadeMs_ > 0)
        return;
    // Off: whatever was faded or waiting goes on at once, at full level.
    fadeTimer_->stop();
    settleTimer_->stop();
    if (deferring_)
        runQueued();
    awaitingRestart_ = false;
    applyLevel(1.0);
}

void AudioPlayer::setOutput(const QByteArray& output, int openslesFramesPerEnqueue)
{
    qCInfo(lcAudioPlayer) << "audio output" << output << "opensles frames per enqueue" << openslesFramesPerEnqueue;
    mpv_set_property_string(mpv_, "ao", output.constData());
    const QByteArray frames = QByteArray::number(qMax(0, openslesFramesPerEnqueue));
    mpv_set_property_string(mpv_, "opensles-frames-per-enqueue", frames.constData());
    // The new options only take with an output opened anew.
    const char* args[] = { "ao-reload", nullptr };
    mpv_command_async(mpv_, 0, args);
}

void AudioPlayer::command(QList<QByteArray> args)
{
    auto run = [this, args = std::move(args)] {
        QVector<const char*> argv;
        for (const QByteArray& arg : args)
            argv.append(arg.constData());
        argv.append(nullptr);
        mpv_command_async(mpv_, 0, argv.data());
    };
    if (deferring_)
        queued_.append(std::move(run));
    else
        run();
}

void AudioPlayer::setProperty(const QByteArray& name, const QByteArray& value)
{
    auto run = [this, name, value] { mpv_set_property_string(mpv_, name.constData(), value.constData()); };
    if (deferring_)
        queued_.append(std::move(run));
    else
        run();
}

void AudioPlayer::setPaused(bool paused) { setProperty("pause", paused ? "yes" : "no"); }

bool AudioPlayer::heard() const { return !paused_ && !stopped_ && !awaitingRestart_ && level_ > 0.0; }

void AudioPlayer::fadeOutFirst()
{
    if (fadeMs_ <= 0 || deferring_ || !heard())
        return;
    qCDebug(lcAudioPlayer) << "fading out over" << qRound(fadeMs_ * level_) << "ms, then" << settleMs_
                           << "ms to settle";
    deferring_ = true;
    fadeClock_.start();
    fadeTo(0.0);
}

void AudioPlayer::startSilent()
{
    if (fadeMs_ <= 0)
        return;
    awaitingRestart_ = true;
    // While fading out it gets to silence before the load goes out anyway.
    if (!deferring_) {
        fadeTimer_->stop();
        applyLevel(0.0);
    }
}

void AudioPlayer::fadeTo(double level)
{
    fadeFrom_ = level_;
    fadeTarget_ = level;
    // A fade from halfway takes half as long: the same slope.
    fadeSpanMs_ = qMax(1, qRound(fadeMs_ * std::abs(level - level_)));
    fadeClock_.start();
    if (!fadeTimer_->isActive())
        fadeTimer_->start();
    onFadeTick();
}

void AudioPlayer::onFadeTick()
{
    const double t = qMin(1.0, double(fadeClock_.elapsed()) / fadeSpanMs_);
    // Raised cosine: no corner where the fade starts or ends.
    const double shape = 0.5 - 0.5 * std::cos(std::numbers::pi * t);
    applyLevel(fadeFrom_ + (fadeTarget_ - fadeFrom_) * shape);
    if (t < 1.0)
        return;
    fadeTimer_->stop();
    if (fadeTarget_ == 0.0 && deferring_)
        settleTimer_->start(settleMs_);
}

void AudioPlayer::runQueued()
{
    deferring_ = false;
    const QList<std::function<void()>> queued = std::exchange(queued_, { });
    qCDebug(lcAudioPlayer) << "silent; running" << queued.size() << "queued after" << fadeClock_.elapsed() << "ms";
    for (const auto& run : queued)
        run();
    // A pause taken back while it faded: up again. A load or a seek comes up
    // on PLAYBACK_RESTART instead.
    if (fadeMs_ > 0 && !paused_ && !stopped_ && !awaitingRestart_)
        fadeTo(1.0);
}

void AudioPlayer::applyLevel(double level)
{
    level_ = level;
    double db = level <= 0.0 ? kSilentDb : qMax(kSilentDb, 20.0 * std::log10(level));
    mpv_set_property(mpv_, "volume-gain", MPV_FORMAT_DOUBLE, &db);
}

void AudioPlayer::setVolume(int volume0To100)
{
    int64_t vol = volume0To100;
    mpv_set_property(mpv_, "volume", MPV_FORMAT_INT64, &vol);
}

void AudioPlayer::setLevelsEnabled(bool enabled)
{
    if (levelsEnabled_ == enabled)
        return;
    levelsEnabled_ = enabled;
    levelFeed_.clear();
    // The analyzer is in mpv's filter chain only while someone reads the
    // levels: with nobody to show them it would just burn CPU (~0.8% of a
    // core). Adding and removing it mid-track is seamless — checked
    // bit-exact against playback without it, over dozens of toggles.
    const char* args[] = { "af", enabled ? "add" : "remove", enabled ? kLevelFilter : kLevelFilterLabel, nullptr };
    if (mpv_command(mpv_, args) < 0)
        qCWarning(lcAudioPlayer) << "couldn't" << (enabled ? "add" : "remove") << "the level analyzer";
    // It prints at ffmpeg's INFO, which mpv logs at "v": asked for only
    // while it runs, the rest of the time it's just mpv's chatter.
    mpv_request_log_messages(mpv_, enabled ? "v" : "info");
}

QVector<LevelReading> AudioPlayer::takeLevels()
{
    double audioPts = 0;
    if (!levelsEnabled_ || mpv_get_property(mpv_, "audio-pts", MPV_FORMAT_DOUBLE, &audioPts) < 0)
        return { };
    return levelFeed_.take(audioPts);
}

} // namespace Playback
