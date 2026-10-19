#include "UpdateChecker.h"

#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "Settings.h"

namespace Update {

namespace {
Q_LOGGING_CATEGORY(lcUpdate, "cloudmus.update")
} // namespace

UpdateChecker::UpdateChecker(Config::Settings& settings, QObject* parent, QNetworkAccessManager* network)
    : QObject(parent)
    , settings_(settings)
    , network_(network != nullptr ? network : &ownedNetwork_)
{
}

void UpdateChecker::configure(QString currentVersion, QUrl feedUrl, QString assetSuffix)
{
    currentVersion_ = std::move(currentVersion);
    feedUrl_ = std::move(feedUrl);
    assetSuffix_ = std::move(assetSuffix);
}

bool UpdateChecker::isDevBuild() const
{
    const std::optional<Version> version = parseVersion(currentVersion_);
    return !version || version->dev;
}

void UpdateChecker::check(bool manual)
{
    manual_ = manual_ || manual;
    if (reply_)
        return;
    QNetworkRequest request(feedUrl_);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CloudMus/%1").arg(currentVersion_));
    request.setTransferTimeout(20000);
    qCDebug(lcUpdate).noquote() << "checking" << feedUrl_.toString() << "for a version after" << currentVersion_
                                << (manual_ ? "(asked for)" : "(scheduled)");
    reply_ = network_->get(request);
    QNetworkReply* reply = reply_;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleReply(reply); });
}

void UpdateChecker::skip(const QString& version) { settings_.setSkippedUpdateVersion(version); }

void UpdateChecker::handleReply(QNetworkReply* reply)
{
    reply->deleteLater();
    reply_ = nullptr;
    const bool manual = manual_;
    manual_ = false;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        const QString error = reply->error() != QNetworkReply::NoError
            ? reply->errorString()
            : tr("The server answered with HTTP %1").arg(status);
        qCWarning(lcUpdate).noquote() << "check failed:" << error;
        emit checkFailed(error, manual);
        return;
    }
    const std::optional<QList<Release>> releases = parseReleases(reply->readAll(), assetSuffix_);
    const std::optional<Version> current = parseVersion(currentVersion_);
    if (!releases || !current) {
        const QString error = !releases ? tr("Unexpected answer from GitHub")
                                        : tr("Unknown version of this build: %1").arg(currentVersion_);
        qCWarning(lcUpdate).noquote() << "check failed:" << error;
        emit checkFailed(error, manual);
        return;
    }
    qCDebug(lcUpdate).noquote() << "the feed has" << releases->size() << "installable releases, the newest"
                                << (releases->isEmpty() ? QStringLiteral("none") : releases->first().name);
    const std::optional<PendingUpdate> update = pendingUpdate(*releases, *current);
    if (!update) {
        qCDebug(lcUpdate).noquote() << currentVersion_ << "is up to date";
        if (manual)
            emit upToDate(releaseOf(*releases, *current));
        return;
    }
    const Release& latest = update->latest();
    if (!manual && latest.name == settings_.skippedUpdateVersion()) {
        qCDebug(lcUpdate).noquote() << latest.name << "is out, but the user skipped it";
        return;
    }
    qCDebug(lcUpdate).noquote() << latest.name << "is out," << update->releases.size() << "release(s) since"
                                << currentVersion_ << "|" << latest.assetName << latest.assetSize << "bytes at"
                                << latest.assetUrl.toString();
    emit updateAvailable(*update, manual);
}

} // namespace Update
