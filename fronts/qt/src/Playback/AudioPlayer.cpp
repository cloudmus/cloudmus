#include "AudioPlayer.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QLoggingCategory>
#include <QMediaPlayer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace Playback {

namespace {
Q_LOGGING_CATEGORY(lcAudioPlayer, "cloudmus.playback.audio")
}

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject(parent)
{
    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    player_->setAudioOutput(audioOutput_);

    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            emit endOfFile();
        }
    });
    connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState)
            emit started();
    });
    connect(player_, &QMediaPlayer::positionChanged, this,
            [this](qint64 posMs) { emit positionChanged(posMs, player_->duration()); });
    connect(player_, &QMediaPlayer::durationChanged, this,
            [this](qint64 durMs) { emit positionChanged(player_->position(), durMs); });
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString& errorString) {
                qCWarning(lcAudioPlayer) << "QMediaPlayer error:" << error << errorString;
                emit failed(errorString);
            });
}

void AudioPlayer::play(const QString& url)
{
    cancelDownload();

    const QUrl parsed(url);
    if (parsed.isLocalFile()) {
        // Local files: read straight off disk via QMediaPlayer's normal
        // source-URL path. None of the remote-URL trouble below applies —
        // no network involved — so there's no reason to buffer them into
        // memory first.
        delete sourceBuffer_;
        sourceBuffer_ = nullptr;
        player_->setSource(parsed);
        player_->play();
        return;
    }

    // Remote URL: download the whole file via Qt's own network stack
    // first, instead of handing QMediaPlayer the raw URL to stream itself.
    // Observed in practice: FFmpeg's built-in HTTP/TLS client (used when
    // QMediaPlayer is given a URL directly) has its connection reset by
    // the CDN partway through a track ("[tls] ... Обрыв канала" / "Demuxing
    // failed -5", no automatic reconnect), which freezes playback in place
    // until a manual seek forces a fresh connection. QNetworkAccessManager
    // doesn't have that problem, and buffering the full track (a few MB for
    // a music file) up front also keeps seeking fully working — a live,
    // in-flight QNetworkReply wouldn't be seekable.
    QNetworkRequest request { parsed };
    reply_ = network_.get(request);
    connect(reply_, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* reply = reply_;
        reply_ = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcAudioPlayer) << "stream download failed:" << reply->errorString();
            emit failed(reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        qCDebug(lcAudioPlayer) << "downloaded" << data.size() << "bytes, starting playback";

        delete sourceBuffer_;
        sourceBuffer_ = new QBuffer(this);
        sourceBuffer_->setData(data);
        sourceBuffer_->open(QIODevice::ReadOnly);
        player_->setSourceDevice(sourceBuffer_);
        player_->play();
    });
}

void AudioPlayer::cancelDownload()
{
    if (reply_ != nullptr) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
}

void AudioPlayer::pause() { player_->pause(); }

void AudioPlayer::resume() { player_->play(); }

void AudioPlayer::stop()
{
    cancelDownload();
    player_->stop();
}

void AudioPlayer::seek(qint64 positionMs) { player_->setPosition(positionMs); }

void AudioPlayer::setVolume(int volume0To100) { audioOutput_->setVolume(static_cast<float>(volume0To100) / 100.0f); }

} // namespace Playback
