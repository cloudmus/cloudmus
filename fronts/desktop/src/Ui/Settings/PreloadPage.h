#pragma once

#include "Settings/Page.h"

class QLabel;
class QProgressBar;
class QSpinBox;

namespace ViewModel {
class Preload;
}

namespace Ui::Settings {

// How far ahead tracks are downloaded, and how much disk they may take —
// all of it ViewModel::Preload's; the page only shows and forwards.
class PreloadPage : public Page {
    Q_OBJECT

public:
    explicit PreloadPage(ViewModel::Preload& preload, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("preload"); }
    QString title() const override;
    QString iconName() const override { return QStringLiteral("playlist_play"); }
    int estimatedHeight() const override { return 205; }

    QWidget* createWidget(QWidget* parent) override;
    bool isDirty() const override;
    Rpc::Task<bool> apply() override;

private:
    void updateUsed();

    // The share of the cache past which its bar turns red, as on mobile.
    static constexpr double kNearlyFull = 0.9;

    ViewModel::Preload& preload_;
    QSpinBox* playlistSpin_ = nullptr;
    QSpinBox* radioSpin_ = nullptr;
    QSpinBox* cacheSpin_ = nullptr;
    QLabel* usedLabel_ = nullptr;
    QProgressBar* usedBar_ = nullptr;
};

} // namespace Ui::Settings
