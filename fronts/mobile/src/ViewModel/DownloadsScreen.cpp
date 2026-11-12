#include "DownloadsScreen.h"

#include <algorithm>

#include "Core.h"

namespace ViewModel {

namespace {
bool isOver(const Downloads::Job& job)
{
    return job.state != Downloads::State::Queued && job.state != Downloads::State::Running;
}
} // namespace

DownloadsScreen::DownloadsScreen(App::Core& core, QObject* parent)
    : QObject(parent)
    , core_(core)
    , rows_({ "jobId", "title", "status", "progress", "isRunning", "isOver", "hasFailed", "isPlaylist" })
{
    connect(&core.downloads(), &Downloads::changed, this, &DownloadsScreen::rebuild);
    connect(&core.nowPlaying(), &NowPlaying::feedbackChanged, this, &DownloadsScreen::canSavePlayingChanged);
    rebuild();
}

bool DownloadsScreen::enabled() const { return core_.downloads().isEnabled(); }

QString DownloadsScreen::directory() const { return core_.settings().downloadDirectory(); }

bool DownloadsScreen::canClear() const
{
    const QList<Downloads::Job>& jobs = core_.downloads().jobs();
    return std::any_of(jobs.cbegin(), jobs.cend(), isOver);
}

bool DownloadsScreen::canSavePlaying() const
{
    const NowPlaying::Feedback feedback = core_.nowPlaying().feedback();
    return feedback.downloadSupported && !feedback.downloadBusy;
}

void DownloadsScreen::setEnabled(bool on)
{
    core_.downloads().setEnabled(on);
    emit enabledChanged();
    emit directoryChanged();
    emit canSavePlayingChanged();
    rebuild();
}

void DownloadsScreen::cancel(int jobId) { core_.downloads().cancel(jobId); }

void DownloadsScreen::clearFinished() { core_.downloads().clearFinished(); }

void DownloadsScreen::savePlaying() { core_.nowPlaying().download(); }

void DownloadsScreen::rebuild()
{
    QList<QVariantHash> rows;
    for (const Downloads::Job& job : core_.downloads().jobs()) {
        rows.append({
            { QStringLiteral("jobId"), job.id },
            { QStringLiteral("title"), job.title },
            { QStringLiteral("status"), Downloads::statusText(job) },
            { QStringLiteral("progress"), job.progress() },
            { QStringLiteral("isRunning"), job.state == Downloads::State::Running },
            { QStringLiteral("isOver"), isOver(job) },
            { QStringLiteral("hasFailed"), job.state == Downloads::State::Failed },
            { QStringLiteral("isPlaylist"), job.isPlaylist },
        });
    }
    rows_.setRows(rows);
    emit canClearChanged();
    emit canSavePlayingChanged();
}

} // namespace ViewModel
