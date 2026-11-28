#pragma once

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

namespace ViewModel {
class UpdateScreen;
}

namespace Ui {

// The whole of installing an update, one step after another in the same
// window, as ViewModel::UpdateScreen takes them: what's new (Install / Skip
// This Version), the download (Cancel), and the install starts as soon as
// it's done (Update::Installer). Closing the window at any step backs out,
// and a download it leaves unfinished is deleted.
class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    UpdateDialog(ViewModel::UpdateScreen& screen, QWidget* parent = nullptr);

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

    // What the screen's step is now.
    void showStep();
    void showOffer();
    void showDownload();
    void showProgress();
    void showFailure(const QString& error);
    void install();
    void openReleasePage();
    void setStep(Step step, const QString& heading, const QString& text);
    QPushButton* addButton(const QString& text, const char* variant);

    ViewModel::UpdateScreen& screen_;
    int shownStep_ = -1;

    QLabel* heading_ = nullptr;
    QLabel* text_ = nullptr;
    QTextBrowser* notes_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

} // namespace Ui
