#include "AudioPulse.h"

#include <algorithm>
#include <optional>

#include "PlaybackController.h"

namespace ViewModel {

namespace {
constexpr int kPollMs = 16;
// Frames come every ~23 ms, so some ticks get none even while playing;
// only a longer gap means the music stopped.
constexpr double kSilentAfterMs = 100;
}

AudioPulse::AudioPulse(Playback::PlaybackController& playback, QObject* parent)
    : AudioPulse(Source { [&playback](bool enabled) { playback.setLevelsEnabled(enabled); },
                     [&playback]() { return playback.takeLevels(); } },
          parent)
{
}

AudioPulse::AudioPulse(Source source, QObject* parent)
    : QObject(parent)
    , source_(std::move(source))
{
    timer_.setInterval(kPollMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this]() { poll(double(clock_.restart())); });
}

void AudioPulse::acquire()
{
    if (holders_++ > 0)
        return;
    detector_.reset();
    source_.setEnabled(true);
    clock_.start();
    timer_.start();
}

void AudioPulse::release()
{
    if (holders_ == 0 || --holders_ > 0)
        return;
    timer_.stop();
    source_.setEnabled(false);
    level_ = 0;
}

void AudioPulse::poll(double dtMs)
{
    const QVector<Playback::LevelReading> readings = source_.take();
    float level = level_;
    std::optional<float> hit;
    msSinceReading_ += dtMs;
    if (readings.isEmpty()) {
        // Paused or between tracks: the level falls away smoothly.
        if (msSinceReading_ > kSilentAfterMs)
            level = detector_.idle(dtMs);
    } else {
        msSinceReading_ = 0;
        for (const Playback::LevelReading& reading : readings) {
            const auto result = detector_.feed(reading);
            level = result.level;
            if (result.beat)
                hit = std::max(hit.value_or(0.0f), *result.beat);
        }
    }
    if (level != level_) {
        level_ = level;
        emit levelChanged(level_);
    }
    if (hit)
        emit beat(*hit);
}

} // namespace ViewModel
