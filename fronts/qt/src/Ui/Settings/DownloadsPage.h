#pragma once

#include "Settings.h"
#include "Settings/Page.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace ViewModel {
class Downloads;
}

namespace Ui::Settings {

class DownloadsPage : public Page {
    Q_OBJECT

public:
    DownloadsPage(Config::Settings& settings, ViewModel::Downloads& downloads, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("downloads"); }
    QString title() const override;
    QString iconName() const override { return QStringLiteral("file_download"); }
    int estimatedHeight() const override { return 170; }

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    Rpc::Task<bool> apply() override;

private:
    Config::Settings::DownloadLayout selectedLayout() const;
    void updateExample();
    // Turning downloads on first asks the user to agree to what they're
    // for; a No leaves them off.
    void onEnabledToggled(bool on);

    Config::Settings& settings_;
    ViewModel::Downloads& downloads_;
    QCheckBox* enabledCheck_ = nullptr;
    QWidget* folderSection_ = nullptr;
    QLineEdit* downloadDirEdit_ = nullptr;
    QComboBox* layoutCombo_ = nullptr;
    QLabel* exampleLabel_ = nullptr;
};

} // namespace Ui::Settings
