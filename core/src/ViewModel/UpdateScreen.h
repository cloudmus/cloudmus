#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

#include "ReleaseFeed.h"

namespace Update {
class UpdateChecker;
class UpdateDownloader;
}

namespace ViewModel {

// An update of the app, for the views, one step after another: what's new
// (install or skip), the download, and the file handed to the platform to
// install. The desktop's update dialog and the mobile front's Update page
// show the same, and the platform does the one step that's its own
// (Update::Installer on the desktop, Android's package installer or a
// store's update flow on Android).
//
// Plain types only: the mobile front reaches it through a Qt Remote Objects
// replica (fronts/mobile/src/Remote/Host.cpp).
class UpdateScreen : public QObject {
    Q_OBJECT
    // Step: what the view shows.
    Q_PROPERTY(int step READ step NOTIFY stepChanged)
    // How this run gets the update (Via).
    Q_PROPERTY(int via READ via NOTIFY offerChanged)
    // The newest version, its release page, the running version ("0.6.0 of
    // 12 September 2026"), and every release's notes since, newest first, as
    // Markdown.
    Q_PROPERTY(QString version READ version NOTIFY offerChanged)
    Q_PROPERTY(QString releasePage READ releasePage NOTIFY offerChanged)
    Q_PROPERTY(QString currentVersionText READ currentVersionText NOTIFY offerChanged)
    Q_PROPERTY(QString notes READ notes NOTIFY offerChanged)
    // Why this run can't install by itself (Via::ReleasePage), for the user.
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY offerChanged)
    // An update is known and not skipped, offered or not: a view may point
    // at it (reopen()).
    Q_PROPERTY(bool available READ available NOTIFY offerChanged)
    // The download: 0..1, or -1 until the size is known; "3 MB of 40 MB ·
    // about 20 s left".
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    // Step::Ready: the file for the platform to install.
    Q_PROPERTY(QString downloadedFile READ downloadedFile NOTIFY stepChanged)
    // Step::Failed: what went wrong.
    Q_PROPERTY(QString error READ error NOTIFY stepChanged)
    // A check asked for by hand is under way; its outcome when nothing is
    // offered (up to date, or the check failed): a heading and rich text.
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(QString checkHeading READ checkHeading NOTIFY checkingChanged)
    Q_PROPERTY(QString checkText READ checkText NOTIFY checkingChanged)

public:
    enum Step {
        None, // nothing to show
        Offer, // what's new: install or skip
        Downloading,
        Ready, // downloaded, for the platform to install
        Installing, // the platform has it (or the store's flow runs)
        Failed,
    };
    Q_ENUM(Step)

    enum Via {
        Here, // downloaded and installed by the app
        Store, // the store the app came from updates it
        ReleasePage, // the user downloads it from the release page
    };
    Q_ENUM(Via)

    explicit UpdateScreen(Update::UpdateChecker& checker, QObject* parent = nullptr);
    ~UpdateScreen() override;

    // Where a release's file is downloaded to (Via::Here).
    void setDownloadPath(std::function<QString(const Update::Release&)> path) { downloadPath_ = std::move(path); }

    int step() const { return step_; }
    int via() const { return via_; }
    QString version() const;
    QString releasePage() const;
    QString currentVersionText() const;
    QString notes() const { return notes_; }
    QString unavailableReason() const { return unavailableReason_; }
    bool available() const { return update_.has_value(); }
    double progress() const { return progress_; }
    QString progressText() const { return progressText_; }
    QString downloadedFile() const { return downloadedFile_; }
    QString error() const { return error_; }
    bool checking() const { return checking_; }
    QString checkHeading() const { return checkHeading_; }
    QString checkText() const { return checkText_; }
    // The offer itself (desktop: the dialog lays the notes out by release).
    const std::optional<Update::PendingUpdate>& pendingUpdate() const { return update_; }

    // "3 October 2026".
    static QString releaseDate(const QDate& date);

public slots:
    // Asked for by hand: an update is offered even if skipped, and the
    // outcome is told (checkHeading/checkText) when there's none.
    void check();
    // At startup, a while in: not for a build between releases, and a
    // skipped version stays quiet.
    void checkAutomatically();
    // This run can't install by itself: the release page instead, `reason`
    // said with the offer (empty: nothing to say).
    void setInstallUnavailable(const QString& reason);
    // The store the app came from has `versionCode` (Android's): that's
    // offered, with the notes of the releases up to it. Via::Store.
    void offerStoreVersion(int versionCode, bool manual);
    // A store's check found nothing (`error` empty) or failed.
    void reportStoreCheck(const QString& error, bool manual);
    // Via::Here: the download. Via::Store: Installing (the view starts the
    // store's flow). Again from Failed.
    void install();
    void skip();
    // The known update, offered again after close().
    void reopen();
    // The view opened the release page (for statistics).
    void releasePageOpened();
    // Backs out at any step: a download is cancelled, a file not handed on
    // is removed. The update stays known (available).
    void close();
    // The platform took the file (Installing), or couldn't; or the user
    // turned the system's question down (back to Ready, the file kept;
    // Via::Store: back to Offer).
    void installStarted();
    void installFailed(const QString& error);
    void installCancelled();
    // The outcome of a check is told: forget it.
    void clearCheck();

signals:
    void stepChanged();
    void offerChanged();
    void progressChanged();
    void checkingChanged();
    // An update for the view to show: found at startup or asked for.
    void offered(bool manual);
    // A step taken, for statistics: "install", "install_launched",
    // "failed", "release_page".
    void stepTaken(const QString& step);

private:
    void takeUpdate(const Update::PendingUpdate& update, bool manual);
    void takeCheckOutcome(const QString& heading, const QString& text, bool manual);
    void setStep(Step step);
    void showProgress(qint64 received, qint64 total, int secondsLeft);
    void fail(const QString& error);
    void discardDownload();

    Update::UpdateChecker& checker_;
    Update::UpdateDownloader* downloader_ = nullptr;
    std::function<QString(const Update::Release&)> downloadPath_;
    std::optional<Update::PendingUpdate> update_;
    // offerStoreVersion() waits for the feed's notes.
    std::optional<Update::Version> storeVersion_;
    bool storeManual_ = false;
    Step step_ = None;
    Via via_ = Here;
    Via defaultVia_ = Here;
    QString notes_;
    QString unavailableReason_;
    double progress_ = -1;
    QString progressText_;
    QString downloadedFile_;
    // Handed to the installer: kept on the way out.
    QString installingFile_;
    QString error_;
    bool checking_ = false;
    QString checkHeading_;
    QString checkText_;
};

} // namespace ViewModel
