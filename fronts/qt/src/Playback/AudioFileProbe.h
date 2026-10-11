#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

struct mpv_handle;

namespace Playback {

// What an audio file on disk is: its codec and bitrate, read from the file
// itself — for a track played from the cache, which its source is never
// asked about again. An mpv of its own, apart from the one that plays:
// it only opens the file, so the same formats are understood and nothing
// is heard. The reading is done on mpv's threads; one file at a time. That
// mpv is there only while there is something to read, and a little after.
class AudioFileProbe : public QObject {
    Q_OBJECT

public:
    struct Info {
        QString codec; // lowercase short name as mpv has it: "opus", "aac", "mp3"
        int bitrateKbps = 0;
    };
    // Either part is empty/0 when the file doesn't tell (or can't be read).
    using Done = std::function<void(const Info&)>;

    explicit AudioFileProbe(QObject* parent = nullptr);
    ~AudioFileProbe() override;

    void probe(const QString& path, Done done);
    // How long the mpv is kept once nothing is left to read.
    void setIdleTimeout(int ms) { idleTimer_.setInterval(ms); }
    bool hasMpv() const { return mpv_ != nullptr; }

private:
    struct Job {
        QString path;
        Done done;
    };

    // Called by libmpv on one of its threads — see AudioPlayer's.
    static void mpvWakeup(void* ctx);
    Q_INVOKABLE void processMpvEvents();
    bool ensureMpv();
    void startNext();
    void finish(const Info& info);
    Info read() const;
    void destroyMpv();

    mpv_handle* mpv_ = nullptr; // created when there is a file to read
    QTimer idleTimer_;
    QList<Job> queue_; // the first is the one being read
    bool busy_ = false;
};

} // namespace Playback
