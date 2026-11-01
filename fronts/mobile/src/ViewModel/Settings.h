#pragma once

#include <QObject>
#include <QSettings>

#include <functional>

namespace ViewModel {

// The core's settings a view can change. Kept in the playback process:
// that's where they are acted on — autoplay is read at boot, before any UI.
class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoplay READ autoplay WRITE setAutoplay NOTIFY autoplayChanged)
    // Another sound (the phone over Bluetooth, a prompt) took the audio for
    // good and paused us: resume once it's over, if it was over within this
    // many seconds. 0: stay paused, as Android has it.
    Q_PROPERTY(int resumeAfterLossSec READ resumeAfterLossSec WRITE setResumeAfterLossSec NOTIFY
                   resumeAfterLossSecChanged)

public:
    explicit Settings(QSettings& store, QObject* parent = nullptr);

    bool autoplay() const;
    int resumeAfterLossSec() const;

    // Called with each new autoplay value: Android mirrors it where the boot
    // receiver reads it without starting the core.
    std::function<void(bool)> onAutoplayChanged;
    // The same for resumeAfterLossSec: the audio focus is Java's.
    std::function<void(int)> onResumeAfterLossSecChanged;

public slots:
    void setAutoplay(bool autoplay);
    void setResumeAfterLossSec(int seconds);

signals:
    void autoplayChanged();
    void resumeAfterLossSecChanged();

private:
    QSettings& store_;
};

} // namespace ViewModel
