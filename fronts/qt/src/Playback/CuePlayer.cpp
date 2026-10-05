#include "CuePlayer.h"

#include <clocale>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QStandardPaths>

#include <mpv/client.h>

namespace Playback {

namespace {
Q_LOGGING_CATEGORY(lcCuePlayer, "cloudmus.playback.cues")

QString resourceName(Cue cue)
{
    switch (cue) {
        case Cue::Like:
            return QStringLiteral("like.oga");
        case Cue::Unlike:
            return QStringLiteral("unlike.oga");
        case Cue::Dislike:
            return QStringLiteral("dislike.oga");
        case Cue::Undislike:
            return QStringLiteral("undislike.oga");
        case Cue::Download:
            return QStringLiteral("download.oga");
    }
    return { };
}
} // namespace

CuePlayer::CuePlayer(QObject* parent)
    : QObject(parent)
{
}

CuePlayer::~CuePlayer()
{
    if (mpv_ != nullptr)
        mpv_terminate_destroy(mpv_);
}

void CuePlayer::play(Cue cue)
{
    const QString path = filePath(cue);
    if (path.isEmpty() || !ensureMpv())
        return;
    const QByteArray pathUtf8 = path.toUtf8();
    const char* args[] = { "loadfile", pathUtf8.constData(), "replace", nullptr };
    mpv_command_async(mpv_, 0, args);
}

bool CuePlayer::ensureMpv()
{
    if (mpv_ != nullptr)
        return true;
    if (mpvFailed_)
        return false;

    // As in AudioPlayer: libmpv needs the C locale's decimal point.
    std::setlocale(LC_NUMERIC, "C");
    mpv_ = mpv_create();
    if (mpv_ == nullptr) {
        mpvFailed_ = true;
        return false;
    }
    // The same outputs as the music, for the same reasons (AudioPlayer.cpp);
    // no pipewire in the AppImage there either.
#ifdef Q_OS_WIN
    const char* output = "wasapi";
#else
    const char* output = qEnvironmentVariableIsSet("APPIMAGE") ? "pulse,alsa" : "pipewire,pulse,alsa";
#endif
    mpv_set_option_string(mpv_, "vid", "no");
    mpv_set_option_string(mpv_, "ao", output);
    mpv_set_option_string(mpv_, "audio-client-name", "CloudMus");
    // Nothing here needs mpv's events, but unread ones pile up in its queue.
    mpv_set_wakeup_callback(
        mpv_,
        [](void* ctx) {
            auto* self = static_cast<CuePlayer*>(ctx);
            QMetaObject::invokeMethod(self, [self]() { self->drainEvents(); }, Qt::QueuedConnection);
        },
        this);
    if (mpv_initialize(mpv_) < 0) {
        qCWarning(lcCuePlayer) << "mpv_initialize failed";
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        mpvFailed_ = true;
        return false;
    }
    return true;
}

void CuePlayer::drainEvents()
{
    while (mpv_ != nullptr) {
        const mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        if (event->event_id == MPV_EVENT_END_FILE) {
            const auto* end = static_cast<const mpv_event_end_file*>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR)
                qCWarning(lcCuePlayer) << "can't play a cue:" << mpv_error_string(end->error);
        }
    }
}

QString CuePlayer::filePath(Cue cue)
{
    const QString name = resourceName(cue);
    const QString resource = QStringLiteral(":/sounds/") + name;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/sounds");
    const QString path = dir + QLatin1Char('/') + name;
    // A copy left by another version may hold another sound.
    if (QFileInfo(path).size() == QFileInfo(resource).size())
        return path;
    QDir().mkpath(dir);
    QFile::remove(path);
    if (!QFile::copy(resource, path)) {
        qCWarning(lcCuePlayer) << "can't copy" << resource << "to" << path;
        return { };
    }
    // QFile::copy keeps the resource's read-only mode: a later version
    // couldn't replace it.
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    return path;
}

} // namespace Playback
