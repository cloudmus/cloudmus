#include "LevelFeed.h"

#include <limits>

namespace Playback {

namespace {

// A few seconds of frames: enough for the ~0.4 s the filter runs ahead,
// bounded when nobody takes them.
constexpr size_t kMaxQueued = 256;
// Timestamps going back by more than this started a new track (gapless),
// not jitter.
constexpr double kRestartSeconds = 0.5;

double parseDb(QByteArrayView text)
{
    bool ok = false;
    const double db = text.trimmed().toDouble(&ok);
    // Digital silence reads "-inf", which toDouble() doesn't take.
    return ok ? db : -std::numeric_limits<double>::infinity();
}

} // namespace

void LevelFeed::addLogLine(QByteArrayView line)
{
    const qsizetype colon = line.indexOf(": ");
    if (colon <= 0 || !line.startsWith("Parsed_ametadata_"))
        return;
    const QByteArray filter = line.first(colon).toByteArray();
    const QByteArrayView text = line.sliced(colon + 2).trimmed();

    if (const qsizetype at = text.indexOf("pts_time:"); at >= 0) {
        bool ok = false;
        const double pts = text.sliced(at + 9).trimmed().toDouble(&ok);
        if (ok)
            currentPts_[filter] = pts;
        return;
    }

    const auto pts = currentPts_.constFind(filter);
    if (pts == currentPts_.cend())
        return;
    const bool full = text.startsWith("lavfi.astats.1.RMS_level=");
    const bool bass = text.startsWith("lavfi.astats.2.RMS_level=");
    if (!full && !bass)
        return;

    Partial& partial = partial_[*pts];
    partial.reading.pts = *pts;
    const double db = parseDb(text.sliced(text.indexOf('=') + 1));
    if (full) {
        partial.reading.fullDb = db;
        partial.hasFull = true;
    } else {
        partial.reading.bassDb = db;
        partial.hasBass = true;
    }
    if (partial.hasFull && partial.hasBass) {
        push(partial.reading);
        partial_.remove(*pts);
    }
    // A frame one of the filters never finished (a seek cut in) mustn't
    // pile up.
    if (partial_.size() > 8)
        partial_.clear();
}

void LevelFeed::push(const LevelReading& reading)
{
    if (lastPushedPts_ >= 0 && reading.pts < lastPushedPts_ - kRestartSeconds)
        ++segment_;
    lastPushedPts_ = reading.pts;
    queue_.push_back({ reading, segment_ });
    while (queue_.size() > kMaxQueued)
        queue_.pop_front();
}

QVector<LevelReading> LevelFeed::take(double audioPts)
{
    // Playback went back in time with frames of a later track queued: the
    // next track has started, and what's left of the last one is past.
    if (lastAudioPts_ >= 0 && audioPts < lastAudioPts_ - kRestartSeconds && !queue_.empty()
        && queue_.back().segment != queue_.front().segment) {
        const int current = queue_.front().segment + 1;
        while (!queue_.empty() && queue_.front().segment < current)
            queue_.pop_front();
    }
    lastAudioPts_ = audioPts;

    QVector<LevelReading> due;
    if (queue_.empty())
        return due;
    const int segment = queue_.front().segment;
    while (!queue_.empty() && queue_.front().segment == segment && queue_.front().reading.pts <= audioPts) {
        due.append(queue_.front().reading);
        queue_.pop_front();
    }
    return due;
}

void LevelFeed::clear()
{
    currentPts_.clear();
    partial_.clear();
    queue_.clear();
    lastPushedPts_ = -1;
    lastAudioPts_ = -1;
}

} // namespace Playback
