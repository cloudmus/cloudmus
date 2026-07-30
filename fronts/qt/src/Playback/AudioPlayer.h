#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QMediaPlayer;
class QAudioOutput;
class QNetworkReply;
class QBuffer;

namespace Playback {

// Thin wrapper around Qt Multimedia's QMediaPlayer + QAudioOutput — pure Qt,
// no third-party library (see fronts/qt/AGENTS.md), and simpler than a
// hand-rolled libmpv binding would be: no manual C API, no dedicated event
// thread — QMediaPlayer already delivers everything via ordinary Qt signals
// on the GUI thread.
//
// Remote (http/https) URLs are downloaded in full via QNetworkAccessManager
// before playback starts, instead of being handed to QMediaPlayer as a raw
// source URL — see the comment on play() for why. This makes starting
// playback asynchronous, hence started()/failed() instead of play() being
// fire-and-forget.
class AudioPlayer : public QObject {
    Q_OBJECT

public:
    explicit AudioPlayer(QObject* parent = nullptr);

    void play(const QString& url);
    void pause();
    void resume();
    void stop();
    void seek(qint64 positionMs);
    void setVolume(int volume0To100);

signals:
    // Playback has actually begun (download, if any, finished and the
    // player started producing audio).
    void started();
    // Download or playback failed; no started()/endOfFile() will follow.
    void failed(QString message);
    // Natural end of the current track (not a manual stop/track change).
    void endOfFile();
    void positionChanged(qint64 positionMs, qint64 durationMs);

private:
    void cancelDownload();

    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOutput_ = nullptr;
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QBuffer* sourceBuffer_ = nullptr;
};

} // namespace Playback
