#pragma once

#include <QDialog>

#include "ReleaseFeed.h"

class QDialogButtonBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

namespace Update {
class UpdateChecker;
class UpdateDownloader;
}

namespace Ui {

// The whole of installing an update, one step after another in the same
// window: what's new (Install / Skip This Version), the download
// (Cancel), and the install starts as soon as it's done. Closing the window at any step backs out, and a download it
// leaves unfinished is deleted.
class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    UpdateDialog(Update::UpdateChecker& checker, Update::PendingUpdate update, QWidget* parent = nullptr);

    void reject() override;

signals:
    // The installer (or the new AppImage) is waiting for the app to exit.
    void quitRequested();

private:
    enum class Step {
        Offer,
        Downloading,
        Failed,
    };

    // "0.1.0 of 12 September 2026", or just the version if its date is unknown.
    QString currentVersionText() const;
    void showOffer();
    void startDownload();
    void showProgress(qint64 received, qint64 total, int secondsLeft);
    void showFailure(const QString& error);
    void install();
    void openReleasePage();
    void setStep(Step step, const QString& heading, const QString& text);
    QPushButton* addButton(const QString& text, const char* variant);

    Update::UpdateChecker& checker_;
    Update::PendingUpdate update_;
    Update::UpdateDownloader* downloader_ = nullptr;
    QString downloadedPath_;

    QLabel* heading_ = nullptr;
    QLabel* text_ = nullptr;
    QTextBrowser* notes_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

} // namespace Ui
