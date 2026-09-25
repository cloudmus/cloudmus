#pragma once

#include "Settings.h"
#include "Settings/Page.h"

class QComboBox;
class QLabel;
class QLineEdit;

namespace Ui::Settings {

class DownloadsPage : public Page {
    Q_OBJECT

public:
    DownloadsPage(Config::Settings& settings, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("downloads"); }
    QString title() const override;
    QString iconName() const override { return QStringLiteral("file_download"); }
    int estimatedHeight() const override { return 110; }

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    Rpc::Task<bool> apply() override;

private:
    Config::Settings::DownloadLayout selectedLayout() const;
    void updateExample();

    Config::Settings& settings_;
    QLineEdit* downloadDirEdit_ = nullptr;
    QComboBox* layoutCombo_ = nullptr;
    QLabel* exampleLabel_ = nullptr;
};

} // namespace Ui::Settings
