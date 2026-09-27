#include "Downloads.h"

#include <QDir>
#include <QJsonObject>
#include <QLoggingCategory>

#include "DownloadPaths.h"
#include "Messages.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "TrackFetch.h"

namespace ViewModel {

namespace {
Q_LOGGING_CATEGORY(lcDownloads, "cloudmus.viewmodel.downloads")

// The protocol's "download cancelled" error (docs/protocol.md §9).
constexpr int kDownloadCancelled = 1410;
// catalog.downloadTrack's timeout. A download that reports progress can
// take as long as a big file needs — progress shows it's alive; one that
// doesn't gets the protocol's recommended minute.
constexpr int kTimeoutWithProgressMs = 30 * 60 * 1000;
constexpr int kTimeoutMs = 60 * 1000;

bool isOver(Downloads::State state)
{
    return state == Downloads::State::Done || state == Downloads::State::Failed || state == Downloads::State::Cancelled;
}

// The running track's own share of its file, 0..1 — 0 while unknown.
double trackFraction(const Downloads::Job& job)
{
    return job.total > 0 ? qBound(0.0, double(job.received) / double(job.total), 1.0) : 0.0;
}
} // namespace

double Downloads::Job::progress() const
{
    if (state == State::Done)
        return 1.0;
    if (!isPlaylist)
        return total > 0 ? trackFraction(*this) : -1.0;
    if (tracks.isEmpty())
        return -1.0;
    return (saved + failed + trackFraction(*this)) / double(tracks.size());
}

QString Downloads::Job::currentTitle() const { return current < tracks.size() ? tracks[current].title : QString(); }

Downloads::Downloads(Rpc::SourceManager& sourceManager, App::SourceSession& sourceSession,
    Library::TrackStates& trackStates, Covers::CoverArtCache& coverArtCache, Config::Settings& settings,
    Messages& messages, QObject* parent)
    : QObject(parent)
    , sourceManager_(sourceManager)
    , trackStates_(trackStates)
    , coverArtCache_(coverArtCache)
    , settings_(settings)
    , messages_(messages)
{
    connect(&sourceSession, &App::SourceSession::downloadProgress, this,
        [this](const QString& sourceId, const DownloadProgressParams& params) {
            onProgress(sourceId, params.downloadId, params.receivedBytes, params.totalBytes);
        });
}

bool Downloads::isEnabled() const { return settings_.downloadsEnabled(); }

void Downloads::setEnabled(bool on)
{
    if (on == isEnabled())
        return;
    settings_.setDownloadsEnabled(on);
    if (!on) {
        for (const Job& job : std::as_const(jobs_))
            cancel(job.id);
        clearFinished();
    }
    emit changed();
}

void Downloads::downloadTrack(const QString& sourceId, const Track& track)
{
    if (!isEnabled())
        return;
    // A new round: what finished before has been told about already.
    if (!isActive())
        clearFinished();
    Job job;
    job.id = nextJobId_++;
    job.sourceId = sourceId;
    job.title = track.title;
    job.tracks = { track };
    jobs_.append(job);
    emit changed();
    startNext();
}

void Downloads::downloadPlaylist(const QString& sourceId, const Playlist& playlist)
{
    if (!isEnabled() || playlist.kind == QStringLiteral("radioStation"))
        return;
    if (!isActive())
        clearFinished();
    Job job;
    job.id = nextJobId_++;
    job.sourceId = sourceId;
    job.title = playlist.title;
    job.isPlaylist = true;
    jobs_.append(job);
    // The playlist itself goes along until run() lists its tracks.
    playlists_.insert(job.id, playlist);
    emit changed();
    startNext();
}

void Downloads::cancel(int jobId)
{
    Job* job = find(jobId);
    if (job == nullptr || isOver(job->state))
        return;
    const bool running = job->state == State::Running;
    job->state = State::Cancelled;
    emit changed();
    if (!running)
        return;
    // Stop the track under way, where the source can (downloadControl);
    // otherwise run() just leaves its result be once it comes.
    Rpc::RpcClient* client = sourceManager_.client(job->sourceId);
    if (client != nullptr && client->available() && !runningDownloadId_.isEmpty()
        && client->capabilities().value(QStringLiteral("downloadControl")).toBool())
        Rpc::catalogCancelDownload(*client, CancelDownloadParams { runningDownloadId_ }).detach();
}

void Downloads::clearFinished()
{
    const qsizetype before = jobs_.size();
    jobs_.removeIf([](const Job& job) { return isOver(job.state); });
    if (jobs_.size() != before)
        emit changed();
}

bool Downloads::isActive() const
{
    return std::any_of(jobs_.cbegin(), jobs_.cend(), [](const Job& job) { return !isOver(job.state); });
}

double Downloads::progress() const
{
    // In tracks: each job counts as many as it has (1 until a playlist's
    // are listed), each done one fully, the running one by its bytes.
    double units = 0;
    double done = 0;
    bool measurable = false;
    for (const Job& job : jobs_) {
        if (isOver(job.state))
            continue;
        const int count = std::max<qsizetype>(1, job.tracks.size());
        units += count;
        done += job.saved + job.failed + trackFraction(job);
        if (job.saved + job.failed > 0 || job.total > 0)
            measurable = true;
    }
    if (units == 0 || !measurable)
        return -1.0;
    return qBound(0.0, done / units, 1.0);
}

bool Downloads::isDownloading(const QString& sourceId, const QString& trackId) const
{
    for (const Job& job : jobs_) {
        if (isOver(job.state) || job.sourceId != sourceId)
            continue;
        for (int i = job.current; i < job.tracks.size(); ++i) {
            if (job.tracks[i].id == trackId)
                return true;
        }
    }
    return false;
}

Downloads::Job* Downloads::find(int jobId)
{
    for (Job& job : jobs_) {
        if (job.id == jobId)
            return &job;
    }
    return nullptr;
}

void Downloads::startNext()
{
    // One track at a time, across all jobs — easy on the services, and on
    // the sources, which download on a worker thread each.
    if (runningJob_ != 0)
        return;
    for (Job& job : jobs_) {
        if (job.state != State::Queued)
            continue;
        job.state = State::Running;
        runningJob_ = job.id;
        emit changed();
        run(job.id).detach();
        return;
    }
}

Rpc::Task<void> Downloads::run(int jobId)
{
    // `job` is looked up again after every co_await: the list may have
    // grown (and moved) meanwhile.
    const QString sourceId = find(jobId)->sourceId;
    QString lastError;

    if (find(jobId)->isPlaylist) {
        try {
            const QVector<Playback::QueueEntry> entries = co_await App::fetchTracks(
                sourceManager_, trackStates_, coverArtCache_, sourceId, playlists_.value(jobId));
            if (Job* job = find(jobId)) {
                for (const Playback::QueueEntry& entry : entries)
                    job->tracks.append(entry.track);
            }
        } catch (const std::exception& e) {
            lastError = QString::fromStdString(e.what());
            qCWarning(lcDownloads) << "listing a playlist to download failed:" << lastError;
        }
        playlists_.remove(jobId);
        emit changed();
    }

    for (;;) {
        Job* job = find(jobId);
        if (job == nullptr || job->state != State::Running || job->current >= job->tracks.size())
            break;
        const Track track = job->tracks[job->current];
        Rpc::RpcClient* client = sourceManager_.client(sourceId);
        if (client == nullptr || !client->available()) {
            lastError = tr("%1 isn't running").arg(sourceId);
            job->failed = int(job->tracks.size()) - job->saved;
            break;
        }
        const QString destDir = Library::downloadDirectoryFor(
            settings_.downloadDirectory(), settings_.downloadLayout(), client->sourceName(), track);
        const bool withProgress = client->capabilities().value(QStringLiteral("downloadControl")).toBool();
        runningDownloadId_ = QStringLiteral("%1-%2").arg(jobId).arg(job->current);
        job->received = 0;
        job->total = -1;
        emit changed();

        bool saved = false;
        bool cancelled = false;
        // docs/protocol.md §7.5: destDir must already exist.
        if (!QDir().mkpath(destDir)) {
            lastError = tr("Can't create the folder %1").arg(destDir);
        } else {
            try {
                const DownloadTrackParams params { track.id, destDir, runningDownloadId_ };
                co_await client->call<DownloadTrackResult>(QStringLiteral("catalog.downloadTrack"), params.toJson(),
                    withProgress ? kTimeoutWithProgressMs : kTimeoutMs);
                saved = true;
            } catch (const Rpc::RpcCallException& e) {
                cancelled = e.error().code == kDownloadCancelled;
                lastError = tr("%1: %2").arg(client->sourceName(), e.error().message);
            } catch (const std::exception& e) {
                lastError = tr("%1: %2").arg(client->sourceName(), QString::fromStdString(e.what()));
            }
        }
        runningDownloadId_.clear();

        job = find(jobId);
        if (job == nullptr)
            break;
        if (cancelled || job->state == State::Cancelled) {
            job->state = State::Cancelled;
            break;
        }
        if (saved) {
            ++job->saved;
        } else {
            ++job->failed;
            qCWarning(lcDownloads) << "downloading" << track.id << "failed:" << lastError;
        }
        job->received = 0;
        job->total = -1;
        ++job->current;
        emit changed();
    }

    if (Job* job = find(jobId))
        finish(*job, lastError);
    runningJob_ = 0;
    emit changed();
    startNext();
}

void Downloads::finish(Job& job, const QString& lastError)
{
    if (job.state == State::Cancelled)
        return;
    job.state = job.saved > 0 ? State::Done : State::Failed;
    const int count = int(job.tracks.size());
    if (!job.isPlaylist) {
        if (job.saved > 0)
            messages_.success(tr("Saved \"%1\"").arg(job.title));
        else
            messages_.error(lastError.isEmpty() ? tr("Couldn't save \"%1\"").arg(job.title) : lastError);
    } else if (count == 0) {
        messages_.error(lastError.isEmpty() ? tr("\"%1\" has no tracks to save").arg(job.title) : lastError);
    } else if (job.failed == 0) {
        messages_.success(tr("Saved %1 tracks of \"%2\"").arg(job.saved).arg(job.title));
    } else {
        messages_.error(tr("Saved %1 of %2 tracks of \"%3\" — %4").arg(job.saved).arg(count).arg(job.title, lastError));
    }
}

void Downloads::onProgress(
    const QString& sourceId, const QString& downloadId, qint64 received, std::optional<qint64> total)
{
    if (downloadId != runningDownloadId_ || runningJob_ == 0)
        return;
    Job* job = find(runningJob_);
    if (job == nullptr || job->sourceId != sourceId || job->state != State::Running)
        return;
    job->received = received;
    job->total = total.value_or(-1);
    emit changed();
}

} // namespace ViewModel
