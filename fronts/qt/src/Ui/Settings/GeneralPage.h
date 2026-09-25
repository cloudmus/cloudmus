#pragma once

#include "Settings/Page.h"

class QCheckBox;

namespace Config {
class Settings;
}

namespace Ui::Settings {

class GeneralPage : public Page {
    Q_OBJECT

public:
    GeneralPage(Config::Settings& settings, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("general"); }
    QString title() const override;
    QString iconName() const override { return QStringLiteral("tune"); }
    int estimatedHeight() const override { return 40; }

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    void apply() override;

private:
    Config::Settings& settings_;
    QCheckBox* closeToTrayCheck_ = nullptr;
};

} // namespace Ui::Settings
