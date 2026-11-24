#include "LogoSound.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "CuePlayer.h"

namespace ViewModel {

namespace {

// Found in cloudmus-logo.mp3 by an onset analysis (spectral flux peaks of the
// mono mix, 1024-sample windows): five strikes, the last the strongest, then
// the handpan ringing on, swelling once more, dying away by 5 s. Seconds of
// the file as mpv counts them: the MP3's first sample is at 0.023 s.
constexpr double kStart = 0.023;
struct Hit {
    double at;
    float strength; // its loudness, as a share of the loudest strike's
};
constexpr Hit kHits[] = {
    { 0.232, 0.67f },
    { 0.488, 0.67f },
    { 0.859, 0.74f },
    { 1.231, 0.62f },
    { 1.486, 1.0f },
};
// Its loudness (RMS) every 0.1 s from the start, as a share of the loudest.
constexpr float kLevels[] = { 0.36f, 0.33f, 0.23f, 0.37f, 0.26f, 0.69f, 0.47f, 0.33f, 0.40f, 0.79f, 0.39f, 0.40f, 0.36f,
    0.54f, 0.42f, 1.0f, 0.85f, 0.71f, 0.68f, 0.58f, 0.52f, 0.63f, 0.52f, 0.46f, 0.48f, 0.48f, 0.53f, 0.61f, 0.69f,
    0.72f, 0.82f, 0.80f, 0.63f, 0.59f, 0.51f, 0.33f, 0.28f, 0.30f, 0.23f, 0.18f, 0.15f, 0.15f, 0.12f, 0.10f, 0.10f,
    0.07f, 0.05f, 0.03f, 0.02f, 0.01f, 0.0f };
constexpr double kLevelStep = 0.1;

constexpr int kStepMs = 16;
// Never sounding (no output, a broken file): give up on it after this.
constexpr int kNeverSoundedMs = 3000;

float levelAt(double seconds)
{
    const double at = seconds / kLevelStep;
    const int i = int(std::floor(at));
    const int last = int(std::size(kLevels)) - 1;
    if (i < 0)
        return 0;
    if (i >= last)
        return kLevels[last];
    const double t = at - i;
    return float(kLevels[i] * (1 - t) + kLevels[i + 1] * t);
}

} // namespace

LogoSound::LogoSound(QObject* parent)
    : QObject(parent)
    , cues_(std::make_unique<Playback::CuePlayer>())
{
    player_ = { [this]() { cues_->play(Playback::Cue::Logo); }, [this]() { return cues_->position(); } };
    connect(cues_.get(), &Playback::CuePlayer::finished, this, &LogoSound::stop);
    timer_.setInterval(kStepMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this]() { step(player_.position()); });
}

LogoSound::LogoSound(Player player, QObject* parent)
    : QObject(parent)
    , player_(std::move(player))
{
    timer_.setInterval(kStepMs);
    connect(&timer_, &QTimer::timeout, this, [this]() { step(player_.position()); });
}

LogoSound::~LogoSound() = default;

void LogoSound::play()
{
    nextHit_ = 0;
    sincePlay_.start();
    if (!playing_) {
        playing_ = true;
        emit playingChanged(true);
    }
    player_.play();
    timer_.start();
}

void LogoSound::step(double position)
{
    if (!playing_)
        return;
    if (position < 0) {
        // Still starting; a sound that never comes isn't waited on forever.
        if (sincePlay_.isValid() && sincePlay_.elapsed() > kNeverSoundedMs)
            stop();
        return;
    }
    const double seconds = position - kStart;
    while (nextHit_ < int(std::size(kHits)) && kHits[nextHit_].at <= seconds)
        emit hit(kHits[nextHit_++].strength);
    setLevel(levelAt(seconds));
    if (seconds >= double(std::size(kLevels) - 1) * kLevelStep)
        stop();
}

void LogoSound::stop()
{
    timer_.stop();
    setLevel(0);
    if (playing_) {
        playing_ = false;
        emit playingChanged(false);
    }
}

void LogoSound::setLevel(float level)
{
    if (level == level_)
        return;
    level_ = level;
    emit levelChanged(level_);
}

} // namespace ViewModel
