#include "SignInHelperDownload.h"

#include <QFile>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "ReleaseFeed.h"
#include "UpdateChecker.h"
#include "UpdateDownloader.h"

Q_LOGGING_CATEGORY(lcSignInHelper, "cloudmus.signinhelper")

namespace ViewModel {

SignInHelperDownload::SignInHelperDownload(Update::UpdateChecker& checker, QObject* parent)
    : QObject(parent)
    , checker_(checker)
{
}

SignInHelperDownload::~SignInHelperDownload() = default;

// The app's own APK's suffix with "-signin": a release's file for this
// device that the app's update never takes for itself.
QString SignInHelperDownload::assetSuffix()
{
    QString suffix = Update::platformAssetSuffix();
    suffix.insert(suffix.lastIndexOf(QLatin1Char('.')), QStringLiteral("-signin"));
    return suffix;
}

void SignInHelperDownload::download()
{
    if (step_ == Downloading || directory_.isEmpty())
        return;
    if (step_ == Ready && QFile::exists(file_))
        return;
    setProgress(-1);
    setStep(Downloading);
    QNetworkRequest request(checker_.feedUrl());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(
        QNetworkRequest::UserAgentHeader, QStringLiteral("CloudMus/%1").arg(checker_.currentVersion()));
    request.setTransferTimeout(20000);
    qCInfo(lcSignInHelper).noquote() << "looking for" << assetSuffix() << "in" << checker_.feedUrl().toString();
    reply_ = checker_.network()->get(request);
    QNetworkReply* reply = reply_;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleFeed(reply); });
}

void SignInHelperDownload::handleFeed(QNetworkReply* reply)
{
    reply->deleteLater();
    // Cancelled meanwhile.
    if (reply != reply_)
        return;
    reply_ = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        fail(reply->errorString());
        return;
    }
    const auto releases = Update::parseReleases(reply->readAll(), assetSuffix());
    if (!releases || releases->isEmpty()) {
        fail(tr("No release here has CloudMus Sign-in for this device."));
        return;
    }
    fetch(releases->first());
}

void SignInHelperDownload::fetch(const Update::Release& release)
{
    if (!downloader_) {
        downloader_ = new Update::UpdateDownloader(*checker_.network(), this);
        connect(downloader_, &Update::UpdateDownloader::progress, this, [this](qint64 received, qint64 total) {
            setProgress(total > 0 ? double(received) / double(total) : -1);
        });
        connect(downloader_, &Update::UpdateDownloader::finished, this, [this](const QString& path) {
            file_ = path;
            emit fileChanged();
            setStep(Ready);
        });
        connect(downloader_, &Update::UpdateDownloader::failed, this, &SignInHelperDownload::fail);
    }
    qCInfo(lcSignInHelper).noquote() << "downloading" << release.assetName;
    downloader_->start(release.assetUrl, release.assetSize, directory_ + QLatin1Char('/') + release.assetName);
}

void SignInHelperDownload::cancel()
{
    if (QNetworkReply* reply = reply_) {
        reply_ = nullptr;
        reply->abort();
    }
    if (downloader_)
        downloader_->cancel();
    if (!file_.isEmpty()) {
        QFile::remove(file_);
        file_.clear();
        emit fileChanged();
    }
    setStep(Idle);
}

void SignInHelperDownload::fail(const QString& error)
{
    qCWarning(lcSignInHelper).noquote() << "download failed:" << error;
    error_ = error;
    emit errorChanged();
    setStep(Failed);
}

void SignInHelperDownload::setStep(Step step)
{
    if (step_ == step)
        return;
    step_ = step;
    emit stepChanged();
}

void SignInHelperDownload::setProgress(double progress)
{
    if (progress_ == progress)
        return;
    progress_ = progress;
    emit progressChanged();
}

} // namespace ViewModel
