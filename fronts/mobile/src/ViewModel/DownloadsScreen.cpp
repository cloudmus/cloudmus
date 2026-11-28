#include "DownloadsScreen.h"

#include <algorithm>

#include "Core.h"

namespace ViewModel {

namespace {
bool isOver(const Downloads::Job& job)
{
    return job.state != Downloads::State::Queued && job.state != Downloads::State::Running;
}

// A running playlist's track, with its size as far as known.
QString detailOf(const Downloads::Job& job)
{
    if (!job.isPlaylist || job.state != Downloads::State::Running || job.currentTitle().isEmpty())
        return { };
    return job.total > 0
        ? DownloadsScreen::tr("%1 — %2").arg(job.currentTitle(), Downloads::sizeText(job.received, job.total))
        : job.currentTitle();
}
} // namespace

DownloadsScreen::DownloadsScreen(App::Core& core, QObject* parent)
    : QObject(parent)
    , core_(core)
    , rows_({ "jobId", "title", "status", "detail", "progress", "isRunning", "isOver", "hasFailed", "isCancelled",
          "isPlaylist" })
{
    connect(&core.downloads(), &Downloads::changed, this, &DownloadsScreen::rebuild);
    connect(&core.downloads(), &Downloads::directoryChanged, this, &DownloadsScreen::directoryChanged);
    rebuild();
}

bool DownloadsScreen::enabled() const { return core_.downloads().isEnabled(); }

QString DownloadsScreen::directory() const { return core_.downloads().directory(); }

int DownloadsScreen::layout() const { return int(core_.settings().downloadLayout()); }

bool DownloadsScreen::active() const { return core_.downloads().isActive(); }

double DownloadsScreen::progress() const { return core_.downloads().progress(); }

int DownloadsScreen::jobCount() const { return int(core_.downloads().jobs().size()); }

bool DownloadsScreen::canClear() const
{
    const QList<Downloads::Job>& jobs = core_.downloads().jobs();
    return std::any_of(jobs.cbegin(), jobs.cend(), isOver);
}

QString DownloadsScreen::summary() const
{
    const QList<Downloads::Job>& jobs = core_.downloads().jobs();
    const auto running = std::find_if(
        jobs.cbegin(), jobs.cend(), [](const Downloads::Job& job) { return job.state == Downloads::State::Running; });
    if (running == jobs.cend())
        return { };
    const qsizetype waiting = std::count_if(
        jobs.cbegin(), jobs.cend(), [](const Downloads::Job& job) { return job.state == Downloads::State::Queued; });
    const QString line = tr("Saving \"%1\": %2").arg(running->title, Downloads::statusText(*running));
    return waiting > 0 ? tr("%1 (%2 more waiting)").arg(line).arg(waiting) : line;
}

DownloadsScreen::Notice DownloadsScreen::notice() const
{
    const QList<Downloads::Job>& jobs = core_.downloads().jobs();
    Notice notice;
    const auto running = std::find_if(
        jobs.cbegin(), jobs.cend(), [](const Downloads::Job& job) { return job.state == Downloads::State::Running; });
    if (running != jobs.cend()) {
        const qsizetype waiting = std::count_if(jobs.cbegin(), jobs.cend(),
            [](const Downloads::Job& job) { return job.state == Downloads::State::Queued; });
        const double progress = core_.downloads().progress();
        notice.shown = true;
        notice.ongoing = true;
        notice.title = running->title;
        notice.text = waiting > 0 ? tr("%1 (%2 more waiting)").arg(Downloads::statusText(*running)).arg(waiting)
                                  : Downloads::statusText(*running);
        notice.permille = progress >= 0 ? int(progress * 1000) : -1;
        return notice;
    }
    // Over: the last that ended other than by the user's cancelling.
    for (auto job = jobs.crbegin(); job != jobs.crend(); ++job) {
        if (job->state == Downloads::State::Cancelled || !isOver(*job))
            continue;
        notice.shown = true;
        notice.failed = job->state == Downloads::State::Failed || job->failed > 0;
        notice.title = job->title;
        notice.text = Downloads::statusText(*job);
        break;
    }
    return notice;
}

void DownloadsScreen::setEnabled(bool on)
{
    core_.downloads().setEnabled(on);
    emit enabledChanged();
    emit directoryChanged();
}

void DownloadsScreen::setLayout(int layout)
{
    using Layout = Config::Settings::DownloadLayout;
    if (layout < int(Layout::Flat) || layout > int(Layout::ByArtistAlbum) || layout == this->layout())
        return;
    core_.settings().setDownloadLayout(Layout(layout));
    emit layoutChanged();
}

void DownloadsScreen::cancel(int jobId) { core_.downloads().cancel(jobId); }

void DownloadsScreen::cancelAll() { core_.downloads().cancelAll(); }

void DownloadsScreen::clearFinished() { core_.downloads().clearFinished(); }

void DownloadsScreen::rebuild()
{
    QList<QVariantHash> rows;
    for (const Downloads::Job& job : core_.downloads().jobs()) {
        rows.append({
            { QStringLiteral("jobId"), job.id },
            { QStringLiteral("title"), job.title },
            { QStringLiteral("status"), Downloads::statusText(job) },
            { QStringLiteral("detail"), detailOf(job) },
            { QStringLiteral("progress"), job.progress() },
            { QStringLiteral("isRunning"), job.state == Downloads::State::Running },
            { QStringLiteral("isOver"), isOver(job) },
            { QStringLiteral("hasFailed"), job.state == Downloads::State::Failed },
            { QStringLiteral("isCancelled"), job.state == Downloads::State::Cancelled },
            { QStringLiteral("isPlaylist"), job.isPlaylist },
        });
    }
    rows_.setRows(rows);
    const auto tell = [this](auto& last, const auto& now, void (DownloadsScreen::*signal)()) {
        if (last == now)
            return;
        last = now;
        emit(this->*signal)();
    };
    tell(active_, active(), &DownloadsScreen::activeChanged);
    tell(progress_, progress(), &DownloadsScreen::progressChanged);
    tell(jobCount_, jobCount(), &DownloadsScreen::jobCountChanged);
    tell(canClear_, canClear(), &DownloadsScreen::canClearChanged);
    tell(summary_, summary(), &DownloadsScreen::summaryChanged);
}

} // namespace ViewModel
