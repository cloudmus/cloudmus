#include "PlatformSettings.h"

namespace ViewModel {

namespace {
const QString kAutoplayKey = QStringLiteral("autoplay");
const QString kResumeAfterLossKey = QStringLiteral("resumeAfterLossSec");
// Keep in step with CoreBridge.DEFAULT_RESUME_AFTER_LOSS_SEC.
constexpr int kResumeAfterLossDefault = 60;
const QString kAudioOutputKey = QStringLiteral("playback/audioOutput");
const QString kOutputChunkKey = QStringLiteral("playback/outputChunkFrames");
const QString kFadeKey = QStringLiteral("playback/fadeMs");
const QString kFadeSettleKey = QStringLiteral("playback/fadeSettleMs");
// Off by default: the fade goes through mpv's volume, which steps once per
// output chunk, and on a phone each step was heard as a tick of its own —
// more clicks than the plain cut it was to soften. Kept as an experiment in
// Developer tools; OpenSL with small chunks is what it needs to be smooth.
const QString kAudioOutputDefault = QStringLiteral("audiotrack");
constexpr int kOutputChunkDefault = 480;
constexpr int kFadeDefault = 0;
constexpr int kFadeSettleDefault = 50;
}

PlatformSettings::PlatformSettings(QSettings& store, QObject* parent)
    : QObject(parent)
    , store_(store)
{
}

bool PlatformSettings::autoplay() const { return store_.value(kAutoplayKey, true).toBool(); }

void PlatformSettings::setAutoplay(bool autoplay)
{
    if (autoplay == this->autoplay())
        return;
    store_.setValue(kAutoplayKey, autoplay);
    store_.sync();
    if (onAutoplayChanged)
        onAutoplayChanged(autoplay);
    emit autoplayChanged();
}

int PlatformSettings::resumeAfterLossSec() const
{
    return store_.value(kResumeAfterLossKey, kResumeAfterLossDefault).toInt();
}

void PlatformSettings::setResumeAfterLossSec(int seconds)
{
    seconds = qMax(0, seconds);
    if (seconds == resumeAfterLossSec())
        return;
    store_.setValue(kResumeAfterLossKey, seconds);
    store_.sync();
    if (onResumeAfterLossSecChanged)
        onResumeAfterLossSecChanged(seconds);
    emit resumeAfterLossSecChanged();
}

} // namespace ViewModel

namespace ViewModel {

int PlatformSettings::intValue(const QString& key, int fallback) const { return store_.value(key, fallback).toInt(); }

bool PlatformSettings::store(const QString& key, const QVariant& value, const QVariant& current)
{
    if (value == current)
        return false;
    store_.setValue(key, value);
    store_.sync();
    return true;
}

QString PlatformSettings::audioOutput() const
{
    const QString output = store_.value(kAudioOutputKey, kAudioOutputDefault).toString();
    return output == QLatin1String("opensles") ? output : kAudioOutputDefault;
}

void PlatformSettings::setAudioOutput(const QString& output)
{
    if (store(kAudioOutputKey, output, audioOutput()))
        emit audioOutputChanged();
}

int PlatformSettings::outputChunkFrames() const { return intValue(kOutputChunkKey, kOutputChunkDefault); }

void PlatformSettings::setOutputChunkFrames(int frames)
{
    if (store(kOutputChunkKey, qMax(0, frames), outputChunkFrames()))
        emit outputChunkFramesChanged();
}

int PlatformSettings::fadeMs() const { return intValue(kFadeKey, kFadeDefault); }

void PlatformSettings::setFadeMs(int ms)
{
    if (store(kFadeKey, qMax(0, ms), fadeMs()))
        emit fadeMsChanged();
}

int PlatformSettings::fadeSettleMs() const { return intValue(kFadeSettleKey, kFadeSettleDefault); }

void PlatformSettings::setFadeSettleMs(int ms)
{
    if (store(kFadeSettleKey, qMax(0, ms), fadeSettleMs()))
        emit fadeSettleMsChanged();
}

} // namespace ViewModel
