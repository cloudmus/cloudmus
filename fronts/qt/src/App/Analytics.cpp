#include "Analytics.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScreen>
#include <QSysInfo>
#include <QTimer>
#include <QUrlQuery>

#include <utility>

#include "Settings.h"

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcAnalytics, "cloudmus.app.analytics")
}

QString languageForAnalytics(const QStringList& uiLanguages)
{
    if (uiLanguages.isEmpty())
        return { };
    // GA4 takes an ISO 639-1 language with an optional region, while
    // uiLanguages() tags may carry a script too ("ru-Cyrl-RU").
    const QLocale locale(uiLanguages.first());
    if (locale.language() == QLocale::C || locale.language() == QLocale::AnyLanguage)
        return { };
    QString language = QLocale::languageToCode(locale.language(), QLocale::ISO639Part1);
    if (language.isEmpty())
        return { }; // no two-letter code for this language
    if (locale.territory() != QLocale::AnyTerritory)
        language += QLatin1Char('-') + QLocale::territoryToCode(locale.territory());
    return language;
}

Analytics::Analytics(Config::Settings& settings, QObject* parent, QNetworkAccessManager* network)
    : QObject(parent)
    , settings_(settings)
    , network_(network != nullptr ? network : &ownedNetwork_)
    , geoLocator_(*network_, this)
    , clientId_(settings.analyticsClientId())
    , sessionId_(QString::number(QDateTime::currentSecsSinceEpoch()))
{
    GeoLocation cached { settings.analyticsCountryId(), settings.analyticsCity(), settings.analyticsRegionId(),
        settings.analyticsContinentId() };
    if (cached.isValid())
        geoLocation_ = cached;
    geoWaitTimer_.setSingleShot(true);
    connect(&geoWaitTimer_, &QTimer::timeout, this, [this]() {
        geoWaitExpired_ = true;
        qCDebug(lcAnalytics) << "geolocation wait expired; sending with cached data if available";
        send();
    });
    connect(&geoLocator_, &GeoLocator::resolved, this, &Analytics::onGeoResolved);
    flushTimer_.setSingleShot(true);
    connect(&flushTimer_, &QTimer::timeout, this, [this]() { finishFlushIfIdle(true); });
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
    if (settings_.analyticsEnabled() && !measurementId_.isEmpty() && !apiSecret_.isEmpty() && endpoint_.isValid())
        geoLocator_.start();
    else
        geoLocator_.stop();
}

void Analytics::setEnabled(bool enabled)
{
    settings_.setAnalyticsEnabled(enabled);
    qCDebug(lcAnalytics) << "collection" << (enabled ? "enabled" : "disabled");
    if (enabled) {
        if (!measurementId_.isEmpty() && !apiSecret_.isEmpty() && endpoint_.isValid())
            geoLocator_.start();
        return;
    }
    geoWaitTimer_.stop();
    geoWaitExpired_ = false;
    geoLocator_.stop();
    qCDebug(lcAnalytics) << "discarding" << pending_.size() << "queued events"
                         << (inFlight_ ? "and aborting the current request" : "");
    pending_.clear();
    if (inFlight_) {
        cancelledInFlight_ = true;
        inFlight_->abort();
    }
}

void Analytics::flush(int timeoutMs, std::function<void()> done)
{
    flushDone_ = std::move(done);
    if (!inFlight_ && pending_.isEmpty()) {
        finishFlushIfIdle();
        return;
    }
    qCDebug(lcAnalytics) << "flushing" << pending_.size() << "queued events"
                         << (inFlight_ ? "after the current request" : "");
    // A lookup still running now would outlast the exit; send with the
    // cached location instead.
    geoWaitExpired_ = true;
    geoWaitTimer_.stop();
    flushTimer_.start(timeoutMs);
    send();
}

void Analytics::finishFlushIfIdle(bool timedOut)
{
    if (!flushDone_ || (!timedOut && (inFlight_ || !pending_.isEmpty())))
        return;
    if (timedOut)
        qCDebug(lcAnalytics) << "flush timed out;" << pending_.size() << "events dropped";
    flushTimer_.stop();
    std::exchange(flushDone_, nullptr)();
}

QJsonObject Analytics::device() const
{
    QJsonObject device { { QStringLiteral("category"), QStringLiteral("desktop") } };
    const QString language = languageForAnalytics(QLocale::system().uiLanguages());
    if (!language.isEmpty())
        device.insert(QStringLiteral("language"), language);
    // Names as GA4 shows them for web traffic, so both land in one bucket.
    const QString kernel = QSysInfo::kernelType();
    if (kernel == QLatin1String("linux")) {
        device.insert(QStringLiteral("operating_system"), QStringLiteral("Linux"));
    } else if (kernel == QLatin1String("winnt")) {
        device.insert(QStringLiteral("operating_system"), QStringLiteral("Windows"));
        device.insert(QStringLiteral("operating_system_version"), QSysInfo::productVersion());
    } else if (kernel == QLatin1String("darwin")) {
        device.insert(QStringLiteral("operating_system"), QStringLiteral("Macintosh"));
        device.insert(QStringLiteral("operating_system_version"), QSysInfo::productVersion());
    }
    // The app stands in for the browser so GA4's browser reports split by
    // player version rather than showing "(not set)".
    device.insert(QStringLiteral("browser"), QStringLiteral("CloudMus"));
    device.insert(QStringLiteral("browser_version"), appVersion_);
    // Logical pixels, as a browser reports screen.width x screen.height.
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QSize size = screen->size();
        if (!size.isEmpty())
            device.insert(
                QStringLiteral("screen_resolution"), QStringLiteral("%1x%2").arg(size.width()).arg(size.height()));
    }
    return device;
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
    if (geoLocator_.resolving() && !geoWaitExpired_) {
        if (!geoWaitTimer_.isActive())
            geoWaitTimer_.start(8000);
        return;
    }
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
    QJsonObject body { { QStringLiteral("client_id"), clientId_ }, { QStringLiteral("events"), events },
        { QStringLiteral("consent"),
            QJsonObject { { QStringLiteral("ad_user_data"), QStringLiteral("DENIED") },
                { QStringLiteral("ad_personalization"), QStringLiteral("DENIED") } } } };
    if (geoLocation_) {
        QJsonObject location { { QStringLiteral("country_id"), geoLocation_->countryId } };
        if (!geoLocation_->city.isEmpty())
            location.insert(QStringLiteral("city"), geoLocation_->city);
        if (!geoLocation_->regionId.isEmpty())
            location.insert(QStringLiteral("region_id"), geoLocation_->regionId);
        if (!geoLocation_->continentId.isEmpty())
            location.insert(QStringLiteral("continent_id"), geoLocation_->continentId);
        body.insert(QStringLiteral("user_location"), location);
    }
    const QJsonObject device = this->device();
    body.insert(QStringLiteral("device"), device);
    qCDebug(lcAnalytics) << "sending with geolocation" << (geoLocation_ ? "available" : "unavailable") << "language"
                         << device.value(QStringLiteral("language")).toString();
    QJsonObject logBody = body;
    logBody.insert(QStringLiteral("client_id"), QStringLiteral("<redacted>"));
    const quint64 requestId = nextRequestId_++;
    qCDebug(lcAnalytics).noquote() << "request" << requestId << "POST"
                                   << endpoint_.toDisplayString(
                                          QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)
                                   << "measurement ID" << measurementId_ << "body"
                                   << QJsonDocument(logBody).toJson(QJsonDocument::Compact);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = network_->post(request, payload);
    if (validateRequests_)
        validate(payload, requestId);
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
        finishFlushIfIdle();
    });
}

void Analytics::validate(const QByteArray& body, quint64 requestId)
{
    // The validation server only checks event names and parameters: it
    // accepts any user_location or device values without complaint.
    QUrl url = endpoint_;
    url.setPath(QStringLiteral("/debug") + endpoint_.path());
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("measurement_id"), measurementId_);
    query.addQueryItem(QStringLiteral("api_secret"), apiSecret_);
    query.addQueryItem(QStringLiteral("validation_behavior"), QStringLiteral("ENFORCE_RECOMMENDATIONS"));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(5000);
    qCDebug(lcAnalytics).noquote() << "request" << requestId << "also POST"
                                   << url.toDisplayString(
                                          QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)
                                   << "for validation";
    QNetworkReply* reply = network_->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonArray messages
            = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("validationMessages")).toArray();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            qCDebug(lcAnalytics) << "request" << requestId << "validation failed, HTTP" << status << "network error"
                                 << int(reply->error());
        } else if (messages.isEmpty()) {
            qCDebug(lcAnalytics) << "request" << requestId << "passed validation";
        } else {
            qCDebug(lcAnalytics).noquote() << "request" << requestId << "validation messages"
                                           << QJsonDocument(messages).toJson(QJsonDocument::Compact);
        }
    });
}

void Analytics::onGeoResolved(std::optional<GeoLocation> location)
{
    geoWaitTimer_.stop();
    geoWaitExpired_ = false;
    if (location) {
        geoLocation_ = *location;
        settings_.setAnalyticsLocation(location->countryId, location->city, location->regionId, location->continentId);
        qCDebug(lcAnalytics) << "using fresh geolocation" << location->countryId << location->regionId
                             << location->continentId << location->city;
    } else {
        qCDebug(lcAnalytics) << "using" << (geoLocation_ ? "cached geolocation" : "no geolocation");
    }
    scheduleSend();
}

} // namespace App
