#include "Settings.h"

namespace ViewModel {

namespace {
const QString kAutoplayKey = QStringLiteral("autoplay");
const QString kResumeAfterLossKey = QStringLiteral("resumeAfterLossSec");
// Keep in step with CoreBridge.DEFAULT_RESUME_AFTER_LOSS_SEC.
constexpr int kResumeAfterLossDefault = 60;
}

Settings::Settings(QSettings& store, QObject* parent)
    : QObject(parent)
    , store_(store)
{
}

bool Settings::autoplay() const { return store_.value(kAutoplayKey, true).toBool(); }

void Settings::setAutoplay(bool autoplay)
{
    if (autoplay == this->autoplay())
        return;
    store_.setValue(kAutoplayKey, autoplay);
    store_.sync();
    if (onAutoplayChanged)
        onAutoplayChanged(autoplay);
    emit autoplayChanged();
}

int Settings::resumeAfterLossSec() const
{
    return store_.value(kResumeAfterLossKey, kResumeAfterLossDefault).toInt();
}

void Settings::setResumeAfterLossSec(int seconds)
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
