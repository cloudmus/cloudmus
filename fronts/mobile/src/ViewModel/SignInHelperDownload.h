#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkReply;

namespace Update {
class UpdateChecker;
class UpdateDownloader;
struct Release;
}

namespace ViewModel {

// The download of CloudMus Sign-in, the helper app that signs in on a device
// whose own browser can't (fronts/mobile/signin-helper): the newest release in
// the update checker's feed that carries its APK for this device. The platform
// installs the file; one the user installed by hand needs none of this.
//
// Plain types only, and a notify signal of its own for each property: the
// views reach it through a Qt Remote Objects replica.
class SignInHelperDownload : public QObject {
    Q_OBJECT
    // Step.
    Q_PROPERTY(int step READ step NOTIFY stepChanged)
    // Step::Downloading: 0..1, or -1 until the size is known.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    // Step::Ready: the APK for the platform to install.
    Q_PROPERTY(QString file READ file NOTIFY fileChanged)
    // The APK a release carries for this device, for installing by hand:
    // "…-arm64-v8a-signin.apk".
    Q_PROPERTY(QString assetSuffix READ assetSuffix NOTIFY assetSuffixChanged)
    // Step::Failed: what went wrong.
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    enum Step {
        Idle,
        Downloading,
        Ready,
        Failed,
    };
    Q_ENUM(Step)

    explicit SignInHelperDownload(Update::UpdateChecker& checker, QObject* parent = nullptr);
    ~SignInHelperDownload() override;

    // Where to download to; with none, nothing is.
    void setDirectory(const QString& directory) { directory_ = directory; }

    int step() const { return step_; }
    double progress() const { return progress_; }
    QString file() const { return file_; }
    static QString assetSuffix();
    QString error() const { return error_; }

public slots:
    // Again from Failed.
    void download();
    // Backs out: the download is cancelled, a file not installed is removed.
    void cancel();

signals:
    void stepChanged();
    void progressChanged();
    void fileChanged();
    // Never: a replica's property needs a signal.
    void assetSuffixChanged();
    void errorChanged();

private:
    void handleFeed(QNetworkReply* reply);
    void fetch(const Update::Release& release);
    void fail(const QString& error);
    void setStep(Step step);
    void setProgress(double progress);

    Update::UpdateChecker& checker_;
    Update::UpdateDownloader* downloader_ = nullptr;
    QPointer<QNetworkReply> reply_;
    QString directory_;
    Step step_ = Idle;
    double progress_ = -1;
    QString file_;
    QString error_;
};

} // namespace ViewModel
