#pragma once

#include "Settings.h"
#include "Settings/Page.h"

class QCheckBox;
class QComboBox;

namespace App {
class Analytics;
}

namespace Ui::Settings {

class GeneralPage : public Page {
    Q_OBJECT

public:
    GeneralPage(Config::Settings& settings, App::Analytics& analytics, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("general"); }
    QString title() const override;
    QString iconName() const override { return QStringLiteral("tune"); }
    int estimatedHeight() const override { return 300; }

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    Rpc::Task<bool> apply() override;

    // Makes the look follow `scheme` (Theme::setModeOverride()): at
    // startup, and on applying this page.
    static void applyColorScheme(Config::Settings::ColorScheme scheme);

private:
    // The glass setting in effect: the user's choice, or the default.
    bool glassWanted() const;

    Config::Settings& settings_;
    App::Analytics& analytics_;
    QCheckBox* launchAtLoginCheck_ = nullptr;
    QCheckBox* startHiddenCheck_ = nullptr;
    QCheckBox* closeToTrayCheck_ = nullptr;
    QCheckBox* trackNotificationsCheck_ = nullptr;
    QComboBox* colorSchemeCombo_ = nullptr;
    QCheckBox* glassCheck_ = nullptr;
    QCheckBox* analyticsCheck_ = nullptr;
    // What launchAtLoginCheck_ started from / was last applied as — the
    // autostart entry lives outside Config::Settings, so isDirty() would
    // otherwise re-read the file every time it's asked.
    bool launchAtLogin_ = false;
};

} // namespace Ui::Settings
