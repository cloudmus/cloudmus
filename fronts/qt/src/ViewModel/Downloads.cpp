#include "Downloads.h"

#include <QDir>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

#include <coroutine>
#include <memory>
#include <stdexcept>
#include <utility>

#include "CoverArtCache.h"
#include "DownloadPaths.h"
#include "Messages.h"
#include "Preloader.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "TrackFetch.h"
#include "TrackFile.h"

namespace ViewModel {

namespace {
Q_LOGGING_CATEGORY(lcDownloads, "cloudmus.viewmodel.downloads")
// Progress of a fetch by the front reaches the jobs this often at most.
constexpr int kProgressIntervalMs = 250;

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
    progressTimer_ = new QTimer(this);
    progressTimer_->setSingleShot(true);
    progressTimer_->setInterval(kProgressIntervalMs);
    connect(progressTimer_, &QTimer::timeout, this, [this]() {
        if (std::exchange(progressPending_, false)) {
            reportFetchProgress();
            progressTimer_->start();
        }
    });
    connect(&sourceSession, &App::SourceSession::downloadProgress, this,
        [this](const QString& sourceId, const DownloadProgressParams& params) {
            onProgress(sourceId, params.downloadId, params.receivedBytes, params.totalBytes);
        });
}

bool Downloads::savesFromStream(const QJsonObject& capabilities)
{
    return capabilities.value(QStringLiteral("downloadFromStream")).toBool()
        && capabilities.value(QStringLiteral("playback")).toObject().value(QStringLiteral("resolveStream")).toBool();
}

bool Downloads::canSave(const QJsonObject& capabilities)
{
    return capabilities.value(QStringLiteral("download")).toBool() || savesFromStream(capabilities);
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
    // Waiting for the front's own download of the track: stop that.
    if (wakePlayerSave_) {
        wakePlayerSave_();
        return;
    }
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
        const QJsonObject capabilities = client->capabilities();
        const bool withProgress = capabilities.value(QStringLiteral("downloadControl")).toBool();
        const bool byTheFront = preloader_ != nullptr && savesFromStream(capabilities);
        const bool bySource = capabilities.value(QStringLiteral("download")).toBool();
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
                // The front saves it where it can; the source itself is
                // the way for one that can't be (or when the stream
                // couldn't be had).
                bool done = false;
                if (byTheFront) {
                    try {
                        co_await saveFromStream(jobId, sourceId, track, destDir);
                        done = true;
                    } catch (const std::exception& e) {
                        if (!bySource)
                            throw;
                        qCInfo(lcDownloads)
                            << "saving" << track.id << "from its stream failed (" << e.what() << "), asking the source";
                    }
                    job = find(jobId);
                    if (job == nullptr || job->state != State::Running)
                        done = true; // cancelled meanwhile: nothing more to do
                }
                if (!done) {
                    if (!bySource)
                        throw std::runtime_error(tr("%1 can't save tracks").arg(client->sourceName()).toStdString());
                    const DownloadTrackParams params { track.id, destDir, runningDownloadId_ };
                    co_await client->call<DownloadTrackResult>(QStringLiteral("catalog.downloadTrack"), params.toJson(),
                        withProgress ? kTimeoutWithProgressMs : kTimeoutMs);
                }
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

namespace {

// What a coroutine waits on while the front fetches a track: resumed once
// by wake(), at once if it already was. Shared, as the signals that wake it
// can outlive the coroutine by a moment.
class Waiter {
public:
    bool await_ready() const noexcept { return woken_; }
    void await_suspend(std::coroutine_handle<> handle) noexcept { handle_ = handle; }
    void await_resume() const noexcept { }

    void wake()
    {
        if (woken_)
            return;
        woken_ = true;
        if (handle_)
            std::exchange(handle_, { }).resume();
    }

private:
    std::coroutine_handle<> handle_;
    bool woken_ = false;
};

// How long a cover that is still on its way is waited for once the track
// is there: past that the file is saved without one.
constexpr int kCoverWaitMs = 5000;
} // namespace

Rpc::Task<void> Downloads::saveFromStream(int jobId, QString sourceId, Track track, QString destDir)
{
    const QPointer<Downloads> alive(this);
    const QString coverUrl
        = track.coverUrl.value_or(track.album && track.album->coverUrl ? *track.album->coverUrl : QString());
    Playback::Preloader& preloader = *preloader_;
    const bool demanded = !preloader.isCached(sourceId, track.id);

    // Wakes from the event loop, not from inside the signal that asks for
    // it: what the coroutine does next (release the track, drop its
    // download) must not happen under the feet of whatever emitted it.
    const auto wakeLater = [this](const std::shared_ptr<Waiter>& waiter) {
        QMetaObject::invokeMethod(this, [waiter]() { waiter->wake(); }, Qt::QueuedConnection);
    };

    if (demanded) {
        // The cover comes along while the track does.
        coverArtCache_.request(coverUrl);

        const auto waiter = std::make_shared<Waiter>();
        const auto matches = [sourceId, id = track.id](const QString& source, const QString& trackId) {
            return source == sourceId && trackId == id;
        };
        const QMetaObject::Connection ready
            = connect(&preloader, &Playback::Preloader::ready, this, [=](const QString& source, const QString& id) {
                  if (matches(source, id))
                      wakeLater(waiter);
              });
        const QMetaObject::Connection failed
            = connect(&preloader, &Playback::Preloader::failed, this, [=](const QString& source, const QString& id) {
                  if (matches(source, id))
                      wakeLater(waiter);
              });
        fetchSource_ = sourceId;
        fetchTrack_ = track.id;
        const QMetaObject::Connection progress
            = connect(&preloader, &Playback::Preloader::progressChanged, this, [this]() {
                  if (progressTimer_->isActive()) {
                      progressPending_ = true;
                      return;
                  }
                  reportFetchProgress();
                  progressTimer_->start();
              });
        wakePlayerSave_ = [=]() { wakeLater(waiter); };

        preloader.demand(Playback::Preloader::Item { sourceId, track.id, track.title, std::nullopt, true });
        // Signals tell only what happens from here on.
        const auto state = preloader.stateOf(sourceId, track.id);
        if (state != Playback::Preloader::State::Ready && state != Playback::Preloader::State::Failed)
            co_await *waiter;

        QObject::disconnect(ready);
        QObject::disconnect(failed);
        QObject::disconnect(progress);
        if (!alive)
            co_return;
        wakePlayerSave_ = nullptr;
        progressTimer_->stop();
        progressPending_ = false;
        fetchSource_.clear();
        fetchTrack_.clear();
    }

    const auto finishDemand = [&]() {
        if (demanded)
            preloader.release(sourceId, track.id);
    };
    const Job* job = find(jobId);
    if (job == nullptr || job->state != State::Running) {
        finishDemand(); // cancelled
        co_return;
    }
    if (preloader.stateOf(sourceId, track.id) != Playback::Preloader::State::Ready) {
        finishDemand();
        throw std::runtime_error(tr("The track couldn't be downloaded").toStdString());
    }
    const QString file = preloader.readyPath(sourceId, track.id);

    // The cover, if it is still on its way — but not for long.
    QImage cover = coverArtCache_.source(coverUrl);
    if (cover.isNull() && !coverUrl.isEmpty()) {
        coverArtCache_.request(coverUrl);
        const auto waiter = std::make_shared<Waiter>();
        const QMetaObject::Connection arrived
            = connect(&coverArtCache_, &Covers::CoverArtCache::pixmapReady, this, [=](const QString& url) {
                  if (url == coverUrl)
                      wakeLater(waiter);
              });
        // A local timer: it goes with this coroutine, not after it.
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, this, [=]() { wakeLater(waiter); });
        timeout.start(kCoverWaitMs);
        co_await *waiter;
        QObject::disconnect(arrived);
        if (!alive)
            co_return;
        cover = coverArtCache_.source(coverUrl);
    }

    const QString saved = Library::saveTrackFile(file, destDir, track, cover);
    finishDemand();
    if (saved.isEmpty())
        throw std::runtime_error(tr("Couldn't write the file to %1").arg(destDir).toStdString());
}

void Downloads::reportFetchProgress()
{
    if (preloader_ == nullptr || runningJob_ == 0 || fetchTrack_.isEmpty())
        return;
    Job* job = find(runningJob_);
    if (job == nullptr || job->state != State::Running)
        return;
    const Playback::Preloader::Transfer transfer = preloader_->transferOf(fetchSource_, fetchTrack_);
    job->received = transfer.received;
    job->total = transfer.total;
    emit changed();
}

void Downloads::finish(Job& job, const QString& lastError)
{
    if (job.state == State::Cancelled)
        return;
    job.state = job.saved > 0 ? State::Done : State::Failed;
    if (job.saved > 0)
        emit completed(job.sourceId, job.isPlaylist, job.saved);
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
