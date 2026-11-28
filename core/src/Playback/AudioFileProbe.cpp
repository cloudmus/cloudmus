#include "AudioFileProbe.h"

#include <QFileInfo>
#include <QLoggingCategory>

#include <clocale>
#include <cmath>

#include <mpv/client.h>

namespace Playback {

namespace {

Q_LOGGING_CATEGORY(lcProbe, "cloudmus.playback.probe")

QString stringProperty(mpv_handle* mpv, const QByteArray& name)
{
    char* value = mpv_get_property_string(mpv, name.constData());
    const QString result = QString::fromUtf8(value);
    mpv_free(value);
    return result;
}

qint64 intProperty(mpv_handle* mpv, const QByteArray& name)
{
    int64_t value = 0;
    return mpv_get_property(mpv, name.constData(), MPV_FORMAT_INT64, &value) >= 0 ? value : 0;
}

} // namespace

AudioFileProbe::AudioFileProbe(QObject* parent)
    : QObject(parent)
{
    // Files are asked about in bursts (a cursor going down a list): the
    // mpv outlives the gaps between them, not the quiet after.
    idleTimer_.setSingleShot(true);
    idleTimer_.setInterval(30000);
    connect(&idleTimer_, &QTimer::timeout, this, &AudioFileProbe::destroyMpv);
}

AudioFileProbe::~AudioFileProbe() { destroyMpv(); }

void AudioFileProbe::destroyMpv()
{
    if (mpv_ == nullptr)
        return;
    mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    mpv_terminate_destroy(mpv_);
    mpv_ = nullptr;
}

bool AudioFileProbe::ensureMpv()
{
    if (mpv_ != nullptr)
        return true;
    // See AudioPlayer: libmpv wants the C locale for numbers.
    std::setlocale(LC_NUMERIC, "C");
    mpv_ = mpv_create();
    if (mpv_ == nullptr)
        return false;
    // Opened and no more: nothing is decoded for anybody to hear or see.
    mpv_set_option_string(mpv_, "vid", "no");
    mpv_set_option_string(mpv_, "ao", "null");
    mpv_set_option_string(mpv_, "pause", "yes");
    mpv_set_option_string(mpv_, "audio-display", "no");
    mpv_set_option_string(mpv_, "ytdl", "no");
    mpv_set_option_string(mpv_, "cache", "no");
    if (mpv_initialize(mpv_) < 0) {
        qCWarning(lcProbe) << "mpv_initialize failed";
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }
    mpv_set_wakeup_callback(mpv_, &AudioFileProbe::mpvWakeup, this);
    return true;
}

void AudioFileProbe::probe(const QString& path, Done done)
{
    idleTimer_.stop();
    queue_.append(Job { path, std::move(done) });
    if (!busy_)
        startNext();
}

void AudioFileProbe::startNext()
{
    while (!queue_.isEmpty()) {
        if (!ensureMpv()) {
            finish({ });
            continue;
        }
        busy_ = true;
        const QByteArray path = queue_.first().path.toUtf8();
        const char* args[] = { "loadfile", path.constData(), nullptr };
        mpv_command_async(mpv_, 0, args);
        return;
    }
    if (mpv_ != nullptr) {
        const char* args[] = { "stop", nullptr };
        mpv_command_async(mpv_, 0, args);
        idleTimer_.start();
    }
}

void AudioFileProbe::finish(const Info& info)
{
    busy_ = false;
    const Job job = queue_.takeFirst();
    if (job.done)
        job.done(info);
}

AudioFileProbe::Info AudioFileProbe::read() const
{
    Info info;
    const qint64 tracks = intProperty(mpv_, "track-list/count");
    for (qint64 i = 0; i < tracks; ++i) {
        const QByteArray track = "track-list/" + QByteArray::number(i);
        if (stringProperty(mpv_, track + "/type") != QLatin1String("audio"))
            continue;
        info.codec = stringProperty(mpv_, track + "/codec");
        info.bitrateKbps = int(std::llround(double(intProperty(mpv_, track + "/demux-bitrate")) / 1000.0));
        break;
    }
    if (info.bitrateKbps <= 0) {
        // The container doesn't say (Opus in WebM): the file's size over
        // its length — the container's own share is a few percent at most
        // for audio alone.
        // The size from the file system, not mpv's "file-size": an older
        // mpv (0.32, the AppImage's) may still say 0 right after loading.
        double duration = 0;
        mpv_get_property(mpv_, "duration", MPV_FORMAT_DOUBLE, &duration);
        const qint64 size = QFileInfo(queue_.first().path).size();
        if (duration > 0 && size > 0)
            info.bitrateKbps = int(std::llround(double(size) * 8.0 / duration / 1000.0));
    }
    return info;
}

void AudioFileProbe::mpvWakeup(void* ctx)
{
    QMetaObject::invokeMethod(static_cast<AudioFileProbe*>(ctx), "processMpvEvents", Qt::QueuedConnection);
}

void AudioFileProbe::processMpvEvents()
{
    // A wakeup may still be on its way when the mpv is gone.
    while (mpv_ != nullptr) {
        const mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        if (!busy_)
            continue; // the end of a file already told about
        if (event->event_id == MPV_EVENT_FILE_LOADED) {
            const Info info = read();
            qCDebug(lcProbe) << queue_.first().path << "is" << info.codec << info.bitrateKbps << "kbps";
            finish(info);
            startNext();
        } else if (event->event_id == MPV_EVENT_END_FILE
            && static_cast<const mpv_event_end_file*>(event->data)->reason == MPV_END_FILE_REASON_ERROR) {
            qCDebug(lcProbe) << "can't read" << queue_.first().path;
            finish({ });
            startNext();
        }
    }
}

} // namespace Playback
