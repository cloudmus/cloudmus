#include "UpdateChecker.h"

#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "Settings.h"

namespace Update {

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
        qWarning() << "Update check failed:" << error;
        emit checkFailed(error, manual);
        return;
    }
    const std::optional<QList<Release>> releases = parseReleases(reply->readAll(), assetSuffix_);
    const std::optional<Version> current = parseVersion(currentVersion_);
    if (!releases || !current) {
        const QString error = !releases ? tr("Unexpected answer from GitHub")
                                        : tr("Unknown version of this build: %1").arg(currentVersion_);
        qWarning() << "Update check failed:" << error;
        emit checkFailed(error, manual);
        return;
    }
    const std::optional<PendingUpdate> update = pendingUpdate(*releases, *current);
    if (!update) {
        if (manual)
            emit upToDate(releaseOf(*releases, *current));
        return;
    }
    if (!manual && update->latest().name == settings_.skippedUpdateVersion())
        return;
    emit updateAvailable(*update, manual);
}

} // namespace Update
