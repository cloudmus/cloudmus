#include "PulseScreen.h"

#include "AudioPulse.h"
#include "LogoSound.h"

namespace ViewModel {

namespace {
// ~30 a second: smooth enough to follow, light on the socket to the views.
constexpr int kLevelIntervalMs = 33;
}

PulseScreen::PulseScreen(AudioPulse& pulse, LogoSound& logo, QObject* parent)
    : QObject(parent)
    , pulse_(pulse)
    , logo_(logo)
    , levelThrottle_(
          kLevelIntervalMs, [this]() { publishLevel(); }, this)
    , logoLevelThrottle_(
          kLevelIntervalMs,
          [this]() {
              if (logo_.level() == logoLevel_)
                  return;
              logoLevel_ = logo_.level();
              emit logoLevelChanged(logoLevel_);
          },
          this)
{
    connect(&logo_, &LogoSound::hit, this, &PulseScreen::logoHit);
    connect(&logo_, &LogoSound::levelChanged, &logoLevelThrottle_, [this]() { logoLevelThrottle_.trigger(); });
    connect(&pulse_, &AudioPulse::levelChanged, &levelThrottle_, [this]() { levelThrottle_.trigger(); });
    // The music's own moves stop while the logo's sound plays (from the tap,
    // its wait for the music to step back included): two pulses at once read
    // as neither in time.
    connect(&pulse_, &AudioPulse::beat, this, [this](float strength) {
        if (!logo_.playing())
            emit beat(strength);
    });
    connect(&logo_, &LogoSound::playingChanged, this, &PulseScreen::publishLevel);
}

PulseScreen::~PulseScreen() { setWatching(false); }

void PulseScreen::setWatching(bool watching)
{
    if (watching == watching_)
        return;
    watching_ = watching;
    if (watching) {
        pulse_.acquire();
    } else {
        pulse_.release();
        levelThrottle_.cancel();
    }
    publishLevel();
    emit watchingChanged(watching_);
}

void PulseScreen::playLogoSound() { logo_.play(); }

void PulseScreen::publishLevel()
{
    const float level = watching_ && !logo_.playing() ? pulse_.level() : 0.0f;
    if (level == level_)
        return;
    level_ = level;
    emit levelChanged(level_);
}

} // namespace ViewModel
