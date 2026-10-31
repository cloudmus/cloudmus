#include "Settings.h"

namespace ViewModel {

namespace {
const QString kAutoplayKey = QStringLiteral("autoplay");
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

} // namespace ViewModel
