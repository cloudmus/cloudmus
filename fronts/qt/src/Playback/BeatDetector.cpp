#include "BeatDetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Playback {

namespace {

constexpr double kAttackMs = 20;
constexpr double kReleaseMs = 250;
// One analysis frame (1024 samples at 44.1 kHz) — the step assumed for the
// first frame, before there's a previous one to measure from.
constexpr double kFrameSeconds = 1024.0 / 44100;
// How far back "lately" reaches.
constexpr double kWindowSeconds = 1.5;
// A kick: its rise is this many standard deviations over the recent rises…
constexpr double kDeviations = 2.0;
// …and at least this share of the recent bass energy.
constexpr double kMinRiseShare = 1.5;
// The bass must be audible at all.
constexpr double kMinBassDb = -45;
// Two beats can't be closer than this.
constexpr double kRefractorySeconds = 0.12;
// How much of "lately" must be heard before it means anything: a track's
// first frames would otherwise make any wobble look like a kick.
constexpr double kWarmupSeconds = 1.0;

// The tempo: from the kicks of the last few seconds, a beat period of
// 50–180 BPM that at least half the intervals between them agree with,
// within a couple of frames. Down to 50 so a kick on every other beat
// (or a slow song) isn't taken for twice the tempo with a kick missing
// between each two — the grid would then fill in off-beats.
constexpr double kTempoMemorySeconds = 6.0;
constexpr double kMinPeriod = 60.0 / 180;
constexpr double kMaxPeriod = 60.0 / 50;
constexpr double kTempoTolerance = 2.5 * kFrameSeconds;
// Two intervals out of three agree by chance too often (a period, its
// double or its triple, give or take a couple of frames).
constexpr size_t kMinAgreeing = 3;
// Kicks not heard but due on the grid: at most this many in a row, and
// softer than the heard ones.
constexpr int kMaxPredictedInRow = 2;
constexpr float kPredictedStrength = 0.6f;

// One step of an exponential follower with the given time constant.
float follow(float current, float target, double dtMs, double timeConstantMs)
{
    return current + (target - current) * float(1 - std::exp(-dtMs / timeConstantMs));
}

double energyOf(double db) { return std::isfinite(db) ? std::pow(10.0, db / 10) : 0.0; }

} // namespace

float BeatDetector::normalize(double db) { return std::clamp(float((db + 50) / 50), 0.0f, 1.0f); }

void BeatDetector::lockTempo(double pts)
{
    onsets_.push_back(pts);
    while (!onsets_.empty() && onsets_.front() <= pts - kTempoMemorySeconds)
        onsets_.pop_front();
    period_ = 0;
    if (onsets_.size() < 4) // three intervals at least
        return;

    std::vector<double> intervals;
    for (size_t i = 1; i < onsets_.size(); ++i)
        intervals.push_back(onsets_[i] - onsets_[i - 1]);
    // How far `interval` is from a whole number (1–3) of beats `period`.
    const auto offGrid = [](double interval, double period, int* beats = nullptr) {
        double best = 1e9;
        for (int n = 1; n <= 3; ++n) {
            if (std::abs(interval - n * period) < best) {
                best = std::abs(interval - n * period);
                if (beats)
                    *beats = n;
            }
        }
        return best;
    };

    // The beat period most intervals agree with: tried from each interval,
    // its half, double and third — a missed kick doubles an interval.
    double bestPeriod = 0;
    size_t bestSupport = 0;
    for (const double interval : intervals) {
        for (const double divisor : { 1.0, 2.0, 0.5, 3.0 }) {
            const double period = interval / divisor;
            if (period < kMinPeriod || period > kMaxPeriod)
                continue;
            size_t support = 0;
            for (const double other : intervals)
                support += offGrid(other, period) <= kTempoTolerance;
            // On a tie the longer one: half of it fits every interval
            // just as well, but would put a beat between the kicks.
            if (support > bestSupport || (support == bestSupport && period > bestPeriod)) {
                bestSupport = support;
                bestPeriod = period;
            }
        }
    }
    if (bestPeriod == 0 || bestSupport < kMinAgreeing || bestSupport * 2 < intervals.size())
        return; // no steady beat to follow

    // Averaged over the intervals that agree, each divided into its beats.
    double sum = 0;
    int count = 0;
    for (const double interval : intervals) {
        int beats = 1;
        if (offGrid(interval, bestPeriod, &beats) <= kTempoTolerance) {
            sum += interval / beats;
            ++count;
        }
    }
    period_ = sum / count;
    nextBeatPts_ = pts + period_;
}

void BeatDetector::reset()
{
    onsets_.clear();
    period_ = 0;
    nextBeatPts_ = 0;
    missedInRow_ = 0;
    heardStrength_ = 0.5f;
    recent_.clear();
    level_ = 0;
    previousPts_ = -1;
    previousEnergy_ = 0;
    lastBeatPts_ = -1e9;
}

float BeatDetector::idle(double dtMs)
{
    level_ = follow(level_, 0, dtMs, kReleaseMs);
    return level_;
}

BeatDetector::Result BeatDetector::feed(const LevelReading& reading)
{
    // Timestamps that go back (another track, a seek) start over.
    if (previousPts_ >= 0 && reading.pts < previousPts_) {
        const float level = level_;
        reset();
        level_ = level;
    }
    const double dtSeconds
        = previousPts_ < 0 ? kFrameSeconds : std::clamp(reading.pts - previousPts_, 0.0, 4 * kFrameSeconds);
    const bool first = previousPts_ < 0;
    previousPts_ = reading.pts;

    const float x = normalize(reading.fullDb);
    level_ = follow(level_, x, dtSeconds * 1000, x > level_ ? kAttackMs : kReleaseMs);
    Result result { level_, std::nullopt };

    const double energy = energyOf(reading.bassDb);
    const double flux = first ? 0 : std::max(0.0, energy - previousEnergy_);
    previousEnergy_ = energy;

    std::optional<float> onset;
    if (!recent_.empty() && reading.pts - recent_.front().pts >= kWarmupSeconds) {
        double meanFlux = 0;
        double meanEnergy = 0;
        for (const Recent& r : recent_) {
            meanFlux += r.flux;
            meanEnergy += r.energy;
        }
        meanFlux /= double(recent_.size());
        meanEnergy /= double(recent_.size());
        double variance = 0;
        for (const Recent& r : recent_)
            variance += (r.flux - meanFlux) * (r.flux - meanFlux);
        const double threshold = meanFlux + kDeviations * std::sqrt(variance / double(recent_.size()));

        if (flux > threshold && flux > kMinRiseShare * meanEnergy && reading.bassDb > kMinBassDb) {
            // How decisively it cleared the bar.
            const double over = flux / std::max({ threshold, kMinRiseShare * meanEnergy, 1e-12 });
            onset = float(std::clamp((over - 1) / 3, 0.25, 1.0));
        }
    }

    const bool canBeat = reading.pts - lastBeatPts_ >= kRefractorySeconds;
    if (onset) {
        missedInRow_ = 0;
        if (canBeat) {
            result.beat = onset;
            lastBeatPts_ = reading.pts;
            heardStrength_ += (*onset - heardStrength_) * 0.3f;
        }
        lockTempo(reading.pts);
    } else if (period_ > 0 && reading.pts >= nextBeatPts_ - kFrameSeconds / 2) {
        // The kick should be here and wasn't heard (lost under the bass, or
        // split across two frames): keep the beat going on the grid — but
        // not for long, or a grid would keep pulsing after the drums stop.
        if (missedInRow_ < kMaxPredictedInRow) {
            if (canBeat) {
                result.beat = kPredictedStrength * heardStrength_;
                lastBeatPts_ = reading.pts;
            }
            ++missedInRow_;
            nextBeatPts_ += period_;
        } else {
            period_ = 0;
        }
    }

    recent_.push_back({ reading.pts, flux, energy });
    while (!recent_.empty() && recent_.front().pts < reading.pts - kWindowSeconds)
        recent_.pop_front();
    return result;
}

} // namespace Playback
