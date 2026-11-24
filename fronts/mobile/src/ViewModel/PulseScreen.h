#pragma once

#include <QObject>

#include "Throttle.h"

namespace ViewModel {

class AudioPulse;
class LogoSound;

// What the music is doing right now, for any view that moves with it: the
// About page's clouds and logo today, visualizations later. The core's
// ViewModel::AudioPulse, offered over Qt Remote Objects to the views in
// another process (Ui::Backend, which counts the views watching and tells
// this only whether any are). New readings go here as properties of their
// own, so every view finds them in one place. The logo's sound
// (ViewModel::LogoSound) too: played from here, its strikes and loudness come
// the same way.
class PulseScreen : public QObject {
    Q_OBJECT
    // 0..1, smooth; 0 while nothing plays or nobody watches.
    Q_PROPERTY(float level READ level NOTIFY levelChanged)
    Q_PROPERTY(bool watching READ watching NOTIFY watchingChanged)
    // How loud the logo's sound is, 0..1; 0 while it doesn't play.
    Q_PROPERTY(float logoLevel READ logoLevel NOTIFY logoLevelChanged)

public:
    PulseScreen(AudioPulse& pulse, LogoSound& logo, QObject* parent = nullptr);
    ~PulseScreen() override;

    float level() const { return level_; }
    bool watching() const { return watching_; }
    float logoLevel() const { return logoLevel_; }

public slots:
    // Whether any view is watching: the analysis runs only then. Saying the
    // same twice changes nothing, so views that come back after the core was
    // away (or the core after the views) can just say it again.
    void setWatching(bool watching);
    // The logo's sound, from the start.
    void playLogoSound();

signals:
    void levelChanged(float level);
    void watchingChanged(bool watching);
    // A kick in the music, as the speakers play it; `strength` 0..1.
    void beat(float strength);
    void logoLevelChanged(float level);
    // A strike of the logo's sound, as heard; `strength` 0..1.
    void logoHit(float strength);

private:
    void publishLevel();

    AudioPulse& pulse_;
    LogoSound& logo_;
    // The level changes on every frame of analysis; in another process the
    // views get it at a steady rate, enough for a smooth swell.
    Timing::Throttle levelThrottle_;
    float level_ = 0;
    bool watching_ = false;
    Timing::Throttle logoLevelThrottle_;
    float logoLevel_ = 0;
};

} // namespace ViewModel
