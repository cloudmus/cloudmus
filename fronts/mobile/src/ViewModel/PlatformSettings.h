#pragma once

#include <QObject>
#include <QSettings>

#include <functional>

namespace ViewModel {

// The core's settings a view can change. Kept in the playback process:
// that's where they are acted on — autoplay is read at boot, before any UI.
class PlatformSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoplay READ autoplay WRITE setAutoplay NOTIFY autoplayChanged)
    // Another sound (the phone over Bluetooth, a prompt) took the audio for
    // good and paused us: resume once it's over, if it was over within this
    // many seconds. 0: stay paused, as Android has it.
    Q_PROPERTY(int resumeAfterLossSec READ resumeAfterLossSec WRITE setResumeAfterLossSec NOTIFY
                   resumeAfterLossSecChanged)
    // Developer tools' playback experiments, see Playback::AudioPlayer's
    // setOutput() and setEdges(): mpv's audio output ("opensles" or
    // "audiotrack"), OpenSL's chunk in frames, the fade at a pause or a track
    // changed by hand and the wait after it.
    Q_PROPERTY(QString audioOutput READ audioOutput WRITE setAudioOutput NOTIFY audioOutputChanged)
    Q_PROPERTY(int outputChunkFrames READ outputChunkFrames WRITE setOutputChunkFrames NOTIFY outputChunkFramesChanged)
    Q_PROPERTY(int fadeMs READ fadeMs WRITE setFadeMs NOTIFY fadeMsChanged)
    Q_PROPERTY(int fadeSettleMs READ fadeSettleMs WRITE setFadeSettleMs NOTIFY fadeSettleMsChanged)

public:
    explicit PlatformSettings(QSettings& store, QObject* parent = nullptr);

    bool autoplay() const;
    int resumeAfterLossSec() const;
    QString audioOutput() const;
    int outputChunkFrames() const;
    int fadeMs() const;
    int fadeSettleMs() const;

    // Called with each new autoplay value: Android mirrors it where the boot
    // receiver reads it without starting the core.
    std::function<void(bool)> onAutoplayChanged;
    // The same for resumeAfterLossSec: the audio focus is Java's.
    std::function<void(int)> onResumeAfterLossSecChanged;

public slots:
    void setAutoplay(bool autoplay);
    void setResumeAfterLossSec(int seconds);
    void setAudioOutput(const QString& output);
    void setOutputChunkFrames(int frames);
    void setFadeMs(int ms);
    void setFadeSettleMs(int ms);

signals:
    void autoplayChanged();
    void resumeAfterLossSecChanged();
    void audioOutputChanged();
    void outputChunkFramesChanged();
    void fadeMsChanged();
    void fadeSettleMsChanged();

private:
    int intValue(const QString& key, int fallback) const;
    // Stores a changed value; false if it was already so.
    bool store(const QString& key, const QVariant& value, const QVariant& current);

    QSettings& store_;
};

} // namespace ViewModel
