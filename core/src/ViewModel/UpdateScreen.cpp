#include "UpdateScreen.h"

#include <QFile>
#include <QLocale>
#include <QLoggingCategory>

#include <utility>

#include "UpdateChecker.h"
#include "UpdateDownloader.h"

namespace ViewModel {

namespace {

Q_LOGGING_CATEGORY(lcUpdate, "cloudmus.update")

QString bold(const QString& text) { return QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped()); }

} // namespace

UpdateScreen::UpdateScreen(Update::UpdateChecker& checker, QObject* parent)
    : QObject(parent)
    , checker_(checker)
{
    connect(&checker_, &Update::UpdateChecker::updateAvailable, this,
        [this](const Update::PendingUpdate& update, bool manual) {
            if (storeVersion_) {
                // Only what the store has: GitHub may be ahead of it.
                Update::PendingUpdate store = update;
                store.releases.removeIf(
                    [this](const Update::Release& release) { return release.version > *storeVersion_; });
                if (store.releases.isEmpty()) {
                    Update::Release release;
                    release.version = *storeVersion_;
                    release.name = storeVersion_->toString();
                    store.releases.append(release);
                }
                takeUpdate(store, storeManual_);
                return;
            }
            takeUpdate(update, manual);
        });
    connect(&checker_, &Update::UpdateChecker::upToDate, this, [this](const std::optional<Update::Release>& own) {
        if (storeVersion_) {
            // The feed is behind the store, or the store is ahead of a
            // dev build: what the store says, with no notes.
            Update::PendingUpdate store;
            Update::Release release;
            release.version = *storeVersion_;
            release.name = storeVersion_->toString();
            store.releases.append(release);
            if (own)
                store.currentPublished = own->published;
            takeUpdate(store, storeManual_);
            return;
        }
        QString text = tr("Version %1 is the latest one.").arg(bold(checker_.currentVersion()));
        if (own && own->published.isValid()) {
            const QString date = releaseDate(own->published);
            // A build between releases has no date of its own.
            text += checker_.isDevBuild() ? tr(" It's built on %1, released on %2.").arg(bold(own->name), date)
                                          : tr(" Released on %1.").arg(date);
        }
        takeCheckOutcome(tr("CloudMus is up to date"), text, true);
    });
    connect(&checker_, &Update::UpdateChecker::checkFailed, this, [this](const QString& error, bool manual) {
        if (storeVersion_) {
            // No notes to be had: the store's version alone.
            Update::PendingUpdate store;
            Update::Release release;
            release.version = *storeVersion_;
            release.name = storeVersion_->toString();
            store.releases.append(release);
            takeUpdate(store, storeManual_);
            return;
        }
        takeCheckOutcome(tr("Couldn't check for updates"), error.toHtmlEscaped(), manual);
    });
}

UpdateScreen::~UpdateScreen() { discardDownload(); }

QString UpdateScreen::version() const { return update_ ? update_->latest().name : QString(); }

QString UpdateScreen::releasePage() const { return update_ ? update_->latest().pageUrl.toString() : QString(); }

QString UpdateScreen::currentVersionText() const
{
    const QDate date = update_ ? update_->currentPublished : QDate();
    // Not "%1 of %2", the download's: a date takes other words in a translation.
    return date.isValid() ? tr("%1 from %2").arg(checker_.currentVersion(), releaseDate(date))
                          : checker_.currentVersion();
}

QString UpdateScreen::releaseDate(const QDate& date)
{
    // QLocale::LongFormat would add the weekday.
    return QLocale().toString(date, QStringLiteral("d MMMM yyyy"));
}

void UpdateScreen::check()
{
    if (step_ == Downloading || step_ == Installing)
        return; // the answer is on screen already
    clearCheck();
    checking_ = true;
    emitCheckingChanged();
    checker_.check(true);
}

void UpdateScreen::checkAutomatically()
{
    if (checker_.isDevBuild()) {
        qCDebug(lcUpdate) << "a build between releases: not checked at startup";
        return;
    }
    checker_.check(false);
}

void UpdateScreen::setInstallUnavailable(const QString& reason)
{
    defaultVia_ = ReleasePage;
    unavailableReason_ = reason;
    if (update_ && via_ == Here) {
        via_ = ReleasePage;
        emitOfferChanged();
    }
}

void UpdateScreen::offerStoreVersion(int versionCode, bool manual)
{
    const std::optional<Update::Version> version = Update::versionFromCode(versionCode);
    const std::optional<Update::Version> current = Update::parseVersion(checker_.currentVersion());
    if (!version || (current && *version <= *current)) {
        reportStoreCheck(QString(), manual);
        return;
    }
    qCDebug(lcUpdate).noquote() << "the store has" << version->toString() << "(code" << versionCode << ")";
    storeVersion_ = version;
    storeManual_ = manual;
    if (manual) {
        checking_ = true;
        emitCheckingChanged();
    }
    // For the notes: manual, so a skipped version is still reported here.
    checker_.check(true);
}

void UpdateScreen::reportStoreCheck(const QString& error, bool manual)
{
    if (!error.isEmpty()) {
        qCWarning(lcUpdate).noquote() << "the store's check failed:" << error;
        takeCheckOutcome(tr("Couldn't check for updates"), error.toHtmlEscaped(), manual);
        return;
    }
    takeCheckOutcome(
        tr("CloudMus is up to date"), tr("Version %1 is the latest one.").arg(bold(checker_.currentVersion())), manual);
}

void UpdateScreen::takeUpdate(const Update::PendingUpdate& update, bool manual)
{
    const bool fromStore = storeVersion_.has_value();
    storeVersion_.reset();
    storeManual_ = false;
    const Update::Release& latest = update.latest();
    if (fromStore && !manual && latest.name == checker_.skippedVersion()) {
        qCDebug(lcUpdate).noquote() << latest.name << "is in the store, but the user skipped it";
        return;
    }
    if (checking_) {
        checking_ = false;
        emitCheckingChanged();
    }
    // A download under way or done stays: it's this offer's, or one the
    // user is already looking at.
    if (step_ == Downloading || step_ == Ready || step_ == Installing) {
        if (manual)
            emit offered(manual);
        return;
    }
    update_ = update;
    via_ = fromStore ? Store : defaultVia_;
    notes_.clear();
    for (const Update::Release& release : update.releases) {
        notes_ += QStringLiteral("**%1**").arg(release.name);
        if (release.published.isValid())
            notes_ += QStringLiteral(" · %1").arg(releaseDate(release.published));
        notes_ += QStringLiteral("\n\n");
        notes_ += release.notes.isEmpty() ? tr("No description.") : release.notes;
        notes_ += QStringLiteral("\n\n");
    }
    error_.clear();
    emitOfferChanged();
    setStep(Offer);
    emit offered(manual);
}

void UpdateScreen::takeCheckOutcome(const QString& heading, const QString& text, bool manual)
{
    storeVersion_.reset();
    storeManual_ = false;
    if (!manual)
        return;
    checking_ = false;
    checkHeading_ = heading;
    checkText_ = text;
    emitCheckingChanged();
}

void UpdateScreen::clearCheck()
{
    if (!checking_ && checkHeading_.isEmpty() && checkText_.isEmpty())
        return;
    checking_ = false;
    checkHeading_.clear();
    checkText_.clear();
    emitCheckingChanged();
}

void UpdateScreen::install()
{
    if (!update_ || (step_ != Offer && step_ != Failed))
        return;
    if (via_ == ReleasePage) {
        releasePageOpened(); // the view opens it
        return;
    }
    emit stepTaken(QStringLiteral("install"));
    if (via_ == Store) {
        setStep(Installing);
        return;
    }
    const Update::Release& latest = update_->latest();
    if (!downloadPath_) {
        fail(tr("This build can't install updates by itself"));
        return;
    }
    if (downloader_ == nullptr) {
        downloader_ = new Update::UpdateDownloader(*checker_.network(), this);
        connect(downloader_, &Update::UpdateDownloader::progress, this, &UpdateScreen::showProgress);
        connect(downloader_, &Update::UpdateDownloader::finished, this, [this](const QString& filePath) {
            downloadedFile_ = filePath;
            qCDebug(lcUpdate).noquote() << "downloaded" << version() << "to" << filePath;
            setStep(Ready);
        });
        connect(downloader_, &Update::UpdateDownloader::failed, this, &UpdateScreen::fail);
    }
    progress_ = -1;
    progressText_ = tr("Connecting…");
    emitProgressChanged();
    error_.clear();
    setStep(Downloading);
    downloader_->start(latest.assetUrl, latest.assetSize, downloadPath_(latest));
}

void UpdateScreen::releasePageOpened() { emit stepTaken(QStringLiteral("release_page")); }

void UpdateScreen::skip()
{
    if (update_)
        checker_.skip(update_->latest().name);
    close();
    // Not pointed at any more (available).
    update_.reset();
    emitOfferChanged();
}

void UpdateScreen::reopen()
{
    if (update_ && step_ == None)
        setStep(Offer);
}

void UpdateScreen::close()
{
    if (step_ == Installing && via_ == Here)
        return; // the platform has the file
    discardDownload();
    setStep(None);
}

void UpdateScreen::installStarted()
{
    if (step_ != Ready && step_ != Installing)
        return;
    qCDebug(lcUpdate).noquote() << "the installer has" << version();
    // From here on the file is the installer's: not removed on the way out.
    installingFile_ = std::exchange(downloadedFile_, QString());
    emit stepTaken(QStringLiteral("install_launched"));
    setStep(Installing);
}

void UpdateScreen::installCancelled()
{
    if (step_ != Installing)
        return;
    qCDebug(lcUpdate).noquote() << "the install of" << version() << "was turned down";
    if (via_ == Store || installingFile_.isEmpty() || !QFile::exists(installingFile_)) {
        installingFile_.clear();
        setStep(Offer);
        return;
    }
    downloadedFile_ = std::exchange(installingFile_, QString());
    setStep(Ready);
}

void UpdateScreen::installFailed(const QString& error)
{
    qCWarning(lcUpdate).noquote() << "couldn't install" << version() << ":" << error;
    if (!installingFile_.isEmpty())
        QFile::remove(std::exchange(installingFile_, QString()));
    discardDownload();
    fail(error);
}

void UpdateScreen::setStep(Step step)
{
    if (step == step_)
        return;
    step_ = step;
    emitStepChanged();
}

void UpdateScreen::emitOfferChanged()
{
    emit viaChanged();
    emit versionChanged();
    emit releasePageChanged();
    emit currentVersionTextChanged();
    emit notesChanged();
    emit unavailableReasonChanged();
    emit availableChanged();
}

void UpdateScreen::emitStepChanged()
{
    // The step last: a replica has the file and the error by then.
    emit downloadedFileChanged();
    emit errorChanged();
    emit stepChanged();
}

void UpdateScreen::emitProgressChanged()
{
    emit progressChanged();
    emit progressTextChanged();
}

void UpdateScreen::emitCheckingChanged()
{
    emit checkingChanged();
    emit checkHeadingChanged();
    emit checkTextChanged();
}

void UpdateScreen::showProgress(qint64 received, qint64 total, int secondsLeft)
{
    const QLocale locale;
    if (total > 0) {
        progress_ = double(received) / double(total);
        progressText_ = tr("%1 of %2").arg(locale.formattedDataSize(received), locale.formattedDataSize(total));
    } else {
        progress_ = -1;
        progressText_ = locale.formattedDataSize(received);
    }
    if (secondsLeft >= 0) {
        // The counts computed outside tr(): lupdate (Qt 6.9) loops forever
        // on qMax(a, b) among its arguments, and drops the text for
        // (a + b) / c.
        const int secondsShown = qMax(secondsLeft, 1);
        const int minutesShown = (secondsLeft + 59) / 60;
        progressText_ += QStringLiteral(" · ")
            + (secondsLeft < 60 ? tr("about %n s left", nullptr, secondsShown)
                                : tr("about %n min left", nullptr, minutesShown));
    }
    emitProgressChanged();
}

void UpdateScreen::fail(const QString& error)
{
    emit stepTaken(QStringLiteral("failed"));
    error_ = error;
    if (step_ == Failed)
        emitStepChanged(); // the new error
    setStep(Failed);
}

void UpdateScreen::discardDownload()
{
    if (downloader_ != nullptr && downloader_->isRunning())
        downloader_->cancel();
    if (!downloadedFile_.isEmpty()) {
        QFile::remove(downloadedFile_);
        downloadedFile_.clear();
    }
}

} // namespace ViewModel
