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

public:
    explicit Settings(QSettings& store, QObject* parent = nullptr);

    bool autoplay() const;

    // Called with each new autoplay value: Android mirrors it where the boot
    // receiver reads it without starting the core.
    std::function<void(bool)> onAutoplayChanged;

public slots:
    void setAutoplay(bool autoplay);

signals:
    void autoplayChanged();

private:
    QSettings& store_;
};

} // namespace ViewModel
