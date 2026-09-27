#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

#include "Coro.h"
#include "Models.h"

namespace App {
class SourceSession;
}
namespace Config {
class Settings;
}
namespace Covers {
class CoverArtCache;
}
namespace Library {
class TrackStates;
}
namespace Rpc {
class SourceManager;
}

namespace ViewModel {

class Messages;

// Saving tracks to the download folder: single tracks and whole playlists,
// queued and downloaded one track at a time, with their progress and a way
// to cancel — for the toolbar's download button and its panel. Progress in
// bytes comes from sources that declare downloadControl (docs/protocol.md
// §7.5); others' downloads just show as under way.
class Downloads : public QObject {
    Q_OBJECT

public:
    enum class State {
        Queued,
        Running, // a playlist's tracks being listed counts too
        Done,
        Failed, // nothing saved (a playlist: none of its tracks)
        Cancelled,
    };

    // One thing the user asked to save: a track, or a playlist's tracks.
    struct Job {
        int id = 0;
        QString sourceId;
        QString title; // the track's, or the playlist's
        bool isPlaylist = false;
        State state = State::Queued;
        QList<Track> tracks; // a playlist's: empty until listed
        int current = 0; // index of the track under way / next
        int saved = 0;
        int failed = 0;
        // The track under way: bytes so far, and the file's size (-1 while
        // unknown — or for good, from a source without downloadControl).
        qint64 received = 0;
        qint64 total = -1;

        // 0..1, or -1 while there's nothing to measure it by.
        double progress() const;
        // Only meaningful while Running: the track under way.
        QString currentTitle() const;
    };

    Downloads(Rpc::SourceManager& sourceManager, App::SourceSession& sourceSession, Library::TrackStates& trackStates,
        Covers::CoverArtCache& coverArtCache, Config::Settings& settings, Messages& messages,
        QObject* parent = nullptr);

    // Downloads are off until the user turns them on in Settings (their
    // folder page asks them to agree to what downloads are for); off, the
    // views offer none of it and nothing here starts.
    bool isEnabled() const;
    // Turning them off cancels whatever is under way.
    void setEnabled(bool on);

    void downloadTrack(const QString& sourceId, const Track& track);
    // A radio station has no fixed list to save — not accepted.
    void downloadPlaylist(const QString& sourceId, const Playlist& playlist);
    // Stops a queued or running job; for a playlist, all of it.
    void cancel(int jobId);
    // Forgets the jobs that are over (the panel's "Clear").
    void clearFinished();

    // Every job still listed: under way, waiting, and over but not cleared.
    const QList<Job>& jobs() const { return jobs_; }
    // Anything queued or running.
    bool isActive() const;
    // All of what's queued or running, 0..1 — or -1 while nothing of it
    // can be measured yet.
    double progress() const;
    // Whether the track is queued or being saved (as a track of its own or
    // in a playlist).
    bool isDownloading(const QString& sourceId, const QString& trackId) const;

signals:
    void changed();
    void completed(const QString& sourceId, bool playlist, int savedCount);

private:
    Job* find(int jobId);
    void startNext();
    Rpc::Task<void> run(int jobId);
    void finish(Job& job, const QString& lastError);
    void onProgress(const QString& sourceId, const QString& downloadId, qint64 received, std::optional<qint64> total);

    Rpc::SourceManager& sourceManager_;
    Library::TrackStates& trackStates_;
    Covers::CoverArtCache& coverArtCache_;
    Config::Settings& settings_;
    Messages& messages_;

    QList<Job> jobs_;
    // Playlist jobs' playlists, until run() has listed their tracks.
    QHash<int, Playlist> playlists_;
    int nextJobId_ = 1;
    // The job being worked on (0: none) and its track's downloadId.
    int runningJob_ = 0;
    QString runningDownloadId_;
};

} // namespace ViewModel
