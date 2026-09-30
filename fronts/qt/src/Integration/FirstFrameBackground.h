#pragma once

#include <QObject>

namespace Integration {

// Windows only: keeps every new top-level window (a dialog, the main
// window) cloaked — hidden by DWM, though shown as far as the app is
// concerned — until it has painted once. Otherwise DWM shows it for a few
// frames before Qt's first one, filled white: a flash, glaring with the
// dark theme. Installed on the application; popups aren't touched.
class FirstPaintCloak : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};

} // namespace Integration
