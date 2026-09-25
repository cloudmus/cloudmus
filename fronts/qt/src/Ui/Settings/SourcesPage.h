#pragma once

#include <QHash>

#include "Settings/Page.h"

class QCheckBox;

namespace Config {
class Settings;
}

namespace Rpc {
class SourceManager;
}

namespace Ui::Settings {

// Which of the discovered backends run: a switched-off one isn't spawned
// and isn't listed in the sidebar. Applying starts or stops them right
// away (Rpc::SourceManager::setEnabled()), not only at the next launch.
class SourcesPage : public Page {
    Q_OBJECT

public:
    SourcesPage(Config::Settings& settings, Rpc::SourceManager& sourceManager, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("sources"); }
    QString title() const override;
    QString iconName() const override { return QStringLiteral("music_note"); }
    int estimatedHeight() const override;

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    void apply() override;

private:
    Config::Settings& settings_;
    Rpc::SourceManager& sourceManager_;
    // Manifest id -> its checkbox.
    QHash<QString, QCheckBox*> checks_;
};

} // namespace Ui::Settings
