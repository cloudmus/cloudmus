#include "Analytics.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

#include <utility>

#include "Settings.h"

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcAnalytics, "cloudmus.app.analytics")
}

Analytics::Analytics(Config::Settings& settings, QObject* parent, QNetworkAccessManager* network)
    : QObject(parent)
    , settings_(settings)
    , network_(network != nullptr ? network : &ownedNetwork_)
    , clientId_(settings.analyticsClientId())
    , sessionId_(QString::number(QDateTime::currentSecsSinceEpoch()))
{
}

void Analytics::configure(QString measurementId, QString apiSecret, QString appVersion, QUrl endpoint)
{
    measurementId_ = std::move(measurementId);
    apiSecret_ = std::move(apiSecret);
    appVersion_ = std::move(appVersion);
    endpoint_ = std::move(endpoint);
    qCDebug(lcAnalytics) << "initialized:"
                         << (measurementId_.isEmpty() || apiSecret_.isEmpty() ? "credentials missing" : "configured")
                         << "measurement ID" << measurementId_ << "collection enabled" << settings_.analyticsEnabled()
                         << "endpoint"
                         << endpoint_.toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo);
}

void Analytics::setEnabled(bool enabled)
{
    settings_.setAnalyticsEnabled(enabled);
    qCDebug(lcAnalytics) << "collection" << (enabled ? "enabled" : "disabled");
    if (enabled)
        return;
    qCDebug(lcAnalytics) << "discarding" << pending_.size() << "queued events"
                         << (inFlight_ ? "and aborting the current request" : "");
    pending_.clear();
    if (inFlight_) {
        cancelledInFlight_ = true;
        inFlight_->abort();
    }
}

QString Analytics::sourceCategory(const QString& sourceId)
{
    if (sourceId == QStringLiteral("yandex-music"))
        return QStringLiteral("yandex_music");
    if (sourceId == QStringLiteral("youtube-music"))
        return QStringLiteral("youtube_music");
    if (sourceId == QStringLiteral("local-folder"))
        return QStringLiteral("local_folder");
    return QStringLiteral("other");
}

void Analytics::recordLaunch() { record(QStringLiteral("app_launch")); }

void Analytics::recordPlayback(const QString& sourceId)
{
    record(QStringLiteral("playback_started"), { { QStringLiteral("source"), sourceCategory(sourceId) } });
}

void Analytics::recordSourceOpened(const QString& sourceId)
{
    record(QStringLiteral("source_opened"), { { QStringLiteral("source"), sourceCategory(sourceId) } });
}

void Analytics::recordDownload(const QString& sourceId, bool playlist, int savedCount)
{
    record(QStringLiteral("download_completed"),
        { { QStringLiteral("source"), sourceCategory(sourceId) },
            { QStringLiteral("kind"), playlist ? QStringLiteral("playlist") : QStringLiteral("track") },
            { QStringLiteral("saved_count"), savedCount } });
}

void Analytics::recordPlaylistChange(const QString& sourceId, bool added)
{
    record(QStringLiteral("playlist_changed"),
        { { QStringLiteral("source"), sourceCategory(sourceId) },
            { QStringLiteral("action"), added ? QStringLiteral("add") : QStringLiteral("remove") } });
}

void Analytics::record(const QString& name, QJsonObject params)
{
    if (!settings_.analyticsEnabled()) {
        qCDebug(lcAnalytics) << "not sending" << name << "because collection is disabled";
        return;
    }
    if (measurementId_.isEmpty() || apiSecret_.isEmpty() || !endpoint_.isValid()) {
        qCDebug(lcAnalytics) << "not sending" << name << "because GA4 is not configured";
        return;
    }
    if (pending_.size() >= 50)
        pending_.removeFirst();
    params.insert(QStringLiteral("session_id"), sessionId_);
    params.insert(QStringLiteral("engagement_time_msec"), 1);
    params.insert(QStringLiteral("app_version"), appVersion_);
    pending_.append({ { QStringLiteral("name"), name }, { QStringLiteral("params"), params } });
    scheduleSend();
}

void Analytics::scheduleSend()
{
    if (sendScheduled_ || inFlight_ || pending_.isEmpty())
        return;
    sendScheduled_ = true;
    QTimer::singleShot(0, this, [this]() {
        sendScheduled_ = false;
        send();
    });
}

void Analytics::send()
{
    if (!settings_.analyticsEnabled() || inFlight_ || pending_.isEmpty())
        return;
    QJsonArray events;
    while (!pending_.isEmpty() && events.size() < 25)
        events.append(pending_.takeFirst());
    QUrl url = endpoint_;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("measurement_id"), measurementId_);
    query.addQueryItem(QStringLiteral("api_secret"), apiSecret_);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(5000);
    const QJsonObject body { { QStringLiteral("client_id"), clientId_ }, { QStringLiteral("events"), events },
        { QStringLiteral("consent"),
            QJsonObject { { QStringLiteral("ad_user_data"), QStringLiteral("DENIED") },
                { QStringLiteral("ad_personalization"), QStringLiteral("DENIED") } } } };
    QJsonObject logBody = body;
    logBody.insert(QStringLiteral("client_id"), QStringLiteral("<redacted>"));
    const quint64 requestId = nextRequestId_++;
    qCDebug(lcAnalytics).noquote() << "request" << requestId << "POST"
                                   << endpoint_.toDisplayString(
                                          QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)
                                   << "measurement ID" << measurementId_ << "body"
                                   << QJsonDocument(logBody).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    inFlight_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (cancelledInFlight_) {
            qCDebug(lcAnalytics) << "request" << requestId << "cancelled because collection was disabled";
        } else if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
            qCDebug(lcAnalytics) << "request" << requestId << "delivered, HTTP" << status;
        } else {
            QString error = reply->errorString();
            error.replace(apiSecret_, QStringLiteral("<redacted>"));
            error.replace(clientId_, QStringLiteral("<redacted>"));
            qCDebug(lcAnalytics) << "request" << requestId << "failed, HTTP" << status << "network error"
                                 << int(reply->error()) << error;
        }
        cancelledInFlight_ = false;
        inFlight_ = nullptr;
        reply->deleteLater();
        scheduleSend();
    });
}

} // namespace App
