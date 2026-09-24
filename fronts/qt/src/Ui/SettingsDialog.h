#pragma once

#include <QDialog>

#include "Settings.h"

class QCheckBox;
class QLineEdit;

namespace Ui {

// Settings… from the hamburger menu. Deliberately small for this pass:
// close-to-tray toggle and download folder. Custom global-hotkey
// rebinding UI is not built yet (Integration::GlobalShortcuts uses a fixed
// default binding set for now) — hardware media keys work regardless via
// MPRIS, which doesn't need any UI here.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(Config::Settings& settings, QWidget* parent = nullptr);

private:
    void save();

    Config::Settings& settings_;
    QCheckBox* closeToTrayCheck_ = nullptr;
    QLineEdit* downloadDirEdit_ = nullptr;
};

} // namespace Ui
