#pragma once

#include "Settings/Page.h"

class QLineEdit;

namespace Config {
class Settings;
}

namespace Ui::Settings {

class DownloadsPage : public Page {
    Q_OBJECT

public:
    DownloadsPage(Config::Settings& settings, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("downloads"); }
    QString title() const override;
    QString iconName() const override { return QStringLiteral("file_download"); }
    int estimatedHeight() const override { return 60; }

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    void apply() override;

private:
    Config::Settings& settings_;
    QLineEdit* downloadDirEdit_ = nullptr;
};

} // namespace Ui::Settings
