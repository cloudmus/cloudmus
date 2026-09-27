#include "Geo.h"

#include "GeoNetworkSignature.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

#include <coroutine>
#include <utility>

namespace App {

namespace {
Q_LOGGING_CATEGORY(lcGeo, "cloudmus.app.geo")

constexpr int kRequestTimeoutMs = 4000;

int cooldownSeconds(const QByteArray& value)
{
    bool ok = false;
    const int seconds = value.trimmed().toInt(&ok);
    if (ok && seconds > 0)
        return qMin(seconds, 86400);
    const QDateTime date = QDateTime::fromString(QString::fromLatin1(value), Qt::RFC2822Date);
    if (date.isValid())
        return int(qBound(qint64(1), QDateTime::currentDateTimeUtc().secsTo(date), qint64(86400)));
    return 60;
}

GeoFetchResult failed(const GeoHttpResponse& response)
{
    GeoError error;
    error.httpStatus = response.httpStatus;
    error.networkError = response.networkError;
    if (response.timedOut || response.networkError == QNetworkReply::TimeoutError) {
        error.kind = GeoErrorKind::Timeout;
    } else if (response.httpStatus == 429) {
        error.kind = GeoErrorKind::RateLimited;
        error.retryAfterSeconds
            = cooldownSeconds(!response.rateLimitTtl.isEmpty() ? response.rateLimitTtl : response.retryAfter);
    } else if (response.networkError != QNetworkReply::NoError) {
        error.kind = GeoErrorKind::Network;
    } else if (response.httpStatus < 200 || response.httpStatus >= 300) {
        error.kind = GeoErrorKind::Http;
    } else {
        error.kind = GeoErrorKind::InvalidResponse;
    }
    return { std::nullopt, error };
}

bool successfulHttp(const GeoHttpResponse& response)
{
    return !response.timedOut && response.networkError == QNetworkReply::NoError && response.httpStatus >= 200
        && response.httpStatus < 300;
}

// Providers give the subdivision part only ("ROS"); GA4 wants the full
// ISO 3166-2 code.
QString regionId(const QString& countryId, const QJsonValue& code)
{
    static const QRegularExpression subdivision(QStringLiteral("^[A-Z0-9]{1,3}$"));
    const QString value = code.toString().trimmed().toUpper();
    if (!subdivision.match(value).hasMatch())
        return { };
    return countryId + QLatin1Char('-') + value;
}

// GA4 takes continents as UN M49 codes; both Americas map to 019 there.
QString continentId(const QJsonValue& code)
{
    const QString value = code.toString().trimmed().toUpper();
    if (value == QLatin1String("AF"))
        return QStringLiteral("002");
    if (value == QLatin1String("AN"))
        return QStringLiteral("010");
    if (value == QLatin1String("AS"))
        return QStringLiteral("142");
    if (value == QLatin1String("EU"))
        return QStringLiteral("150");
    if (value == QLatin1String("NA") || value == QLatin1String("SA"))
        return QStringLiteral("019");
    if (value == QLatin1String("OC"))
        return QStringLiteral("009");
    return { };
}

QString errorName(GeoErrorKind kind)
{
    switch (kind) {
        case GeoErrorKind::Timeout:
            return QStringLiteral("timeout");
        case GeoErrorKind::Network:
            return QStringLiteral("network");
        case GeoErrorKind::Http:
            return QStringLiteral("HTTP");
        case GeoErrorKind::RateLimited:
            return QStringLiteral("rate limit");
        case GeoErrorKind::InvalidResponse:
            return QStringLiteral("invalid response");
        case GeoErrorKind::Rejected:
            return QStringLiteral("provider rejected request");
    }
    return QStringLiteral("unknown");
}
} // namespace

bool GeoLocation::isValid() const
{
    if (countryId.size() != 2)
        return false;
    for (QChar character : countryId) {
        if (character < QLatin1Char('A') || character > QLatin1Char('Z'))
            return false;
    }
    return true;
}

class GeoHttpAwaiter {
public:
    GeoHttpAwaiter(GeoHttpClient& client, QUrl url)
        : client_(client)
        , url_(std::move(url))
    {
    }

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> continuation)
    {
        QNetworkRequest request(url_);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(kRequestTimeoutMs);
        QNetworkReply* reply = client_.network_.get(request);
        client_.activeReply_ = reply;
        auto* timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [this, reply]() {
            timedOut_ = true;
            reply->abort();
        });
        QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, timer, continuation]() {
            timer->stop();
            response_.body = reply->readAll();
            response_.retryAfter = reply->rawHeader("Retry-After");
            response_.rateLimitTtl = reply->rawHeader("X-Ttl");
            response_.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            response_.networkError = int(reply->error());
            response_.timedOut = timedOut_;
            client_.activeReply_ = nullptr;
            reply->deleteLater();
            continuation.resume();
        });
        timer->start(kRequestTimeoutMs);
    }

    GeoHttpResponse await_resume() { return std::move(response_); }

private:
    GeoHttpClient& client_;
    QUrl url_;
    GeoHttpResponse response_;
    bool timedOut_ = false;
};

Rpc::Task<GeoHttpResponse> GeoHttpClient::get(const QUrl& url)
{
    qCDebug(lcGeo) << "GET" << url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveFragment);
    co_return co_await GeoHttpAwaiter(*this, url);
}

void GeoHttpClient::abort()
{
    if (activeReply_)
        activeReply_->abort();
}

QString IpWhoIsFetcher::name() const { return QStringLiteral("ipwho.is"); }

Rpc::Task<GeoFetchResult> IpWhoIsFetcher::fetch(GeoHttpClient& http)
{
    const GeoHttpResponse response = co_await http.get(QUrl(QStringLiteral("https://ipwho.is/")));
    if (!successfulHttp(response))
        co_return failed(response);

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    if (!document.isObject())
        co_return failed(response);
    const QJsonObject data = document.object();
    if (data.value(QStringLiteral("success")) != true) {
        const QString message = data.value(QStringLiteral("message")).toString();
        if (message.contains(QStringLiteral("limit"), Qt::CaseInsensitive))
            co_return GeoFetchResult { std::nullopt,
                { GeoErrorKind::RateLimited, response.httpStatus, cooldownSeconds(response.retryAfter) } };
        co_return GeoFetchResult { std::nullopt, { GeoErrorKind::Rejected, response.httpStatus } };
    }
    GeoLocation location { data.value(QStringLiteral("country_code")).toString().trimmed().toUpper(),
        data.value(QStringLiteral("city")).toString().trimmed().left(100) };
    location.regionId = regionId(location.countryId, data.value(QStringLiteral("region_code")));
    location.continentId = continentId(data.value(QStringLiteral("continent_code")));
    if (!location.isValid())
        co_return failed(response);
    co_return GeoFetchResult { location, { } };
}

QString IpApiFetcher::name() const { return QStringLiteral("ip-api.com"); }

Rpc::Task<GeoFetchResult> IpApiFetcher::fetch(GeoHttpClient& http)
{
    const GeoHttpResponse response = co_await http.get(
        QUrl(QStringLiteral("http://ip-api.com/json/?fields=status,message,continentCode,countryCode,region,city")));
    if (!successfulHttp(response))
        co_return failed(response);

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    if (!document.isObject())
        co_return failed(response);
    const QJsonObject data = document.object();
    if (data.value(QStringLiteral("status")).toString() != QStringLiteral("success")) {
        const QString message = data.value(QStringLiteral("message")).toString();
        if (message.contains(QStringLiteral("limit"), Qt::CaseInsensitive))
            co_return GeoFetchResult { std::nullopt,
                { GeoErrorKind::RateLimited, response.httpStatus, cooldownSeconds(response.rateLimitTtl) } };
        co_return GeoFetchResult { std::nullopt, { GeoErrorKind::Rejected, response.httpStatus } };
    }
    GeoLocation location { data.value(QStringLiteral("countryCode")).toString().trimmed().toUpper(),
        data.value(QStringLiteral("city")).toString().trimmed().left(100) };
    location.regionId = regionId(location.countryId, data.value(QStringLiteral("region")));
    location.continentId = continentId(data.value(QStringLiteral("continentCode")));
    if (!location.isValid())
        co_return failed(response);
    co_return GeoFetchResult { location, { } };
}

GeoLocator::GeoLocator(
    QNetworkAccessManager& network, QObject* parent, std::vector<std::unique_ptr<GeoFetcher>> fetchers)
    : QObject(parent)
    , http_(network)
    , fetchers_(std::move(fetchers))
{
    if (fetchers_.empty()) {
        fetchers_.push_back(std::make_unique<IpWhoIsFetcher>());
        fetchers_.push_back(std::make_unique<IpApiFetcher>());
    }
    retryAt_.resize(fetchers_.size());
    pollTimer_.setInterval(15000);
    connect(&pollTimer_, &QTimer::timeout, this, &GeoLocator::checkNetwork);
    debounceTimer_.setInterval(2000);
    debounceTimer_.setSingleShot(true);
    connect(&debounceTimer_, &QTimer::timeout, this, &GeoLocator::refresh);
}

GeoLocator::~GeoLocator() { stop(); }

void GeoLocator::start()
{
    if (active_)
        return;
    active_ = true;
    if (!networkSignalsConnected_ && QNetworkInformation::loadDefaultBackend()) {
        if (auto* information = QNetworkInformation::instance()) {
            connect(information, &QNetworkInformation::reachabilityChanged, this, [this]() {
                if (active_)
                    debounceTimer_.start();
            });
            connect(information, &QNetworkInformation::transportMediumChanged, this, [this]() {
                if (active_)
                    debounceTimer_.start();
            });
            networkSignalsConnected_ = true;
        }
    }
    signature_ = geoNetworkSignature();
    pollTimer_.start();
    refresh();
}

void GeoLocator::stop()
{
    active_ = false;
    ++generation_;
    pollTimer_.stop();
    debounceTimer_.stop();
    http_.abort();
    resolving_ = false;
}

void GeoLocator::refresh()
{
    if (!active_)
        return;
    const quint64 generation = ++generation_;
    http_.abort();
    resolving_ = true;
    qCDebug(lcGeo) << "starting lookup" << generation;
    resolve(generation).detach();
}

Rpc::Task<void> GeoLocator::resolve(quint64 generation)
{
    for (size_t index = 0; index < fetchers_.size(); ++index) {
        if (!active_ || generation != generation_)
            co_return;
        const auto& fetcher = fetchers_[index];
        if (retryAt_[index] > QDateTime::currentDateTimeUtc()) {
            qCDebug(lcGeo) << fetcher->name() << "skipped until rate limit resets"
                           << retryAt_[index].toString(Qt::ISODate);
            continue;
        }
        QElapsedTimer elapsed;
        elapsed.start();
        qCDebug(lcGeo) << "trying" << fetcher->name();
        GeoFetchResult result;
        try {
            result = co_await fetcher->fetch(http_);
        } catch (const std::exception&) {
            result.error.kind = GeoErrorKind::Network;
        }
        if (!active_ || generation != generation_)
            co_return;
        if (result.location && result.location->isValid()) {
            qCDebug(lcGeo) << fetcher->name() << "succeeded in" << elapsed.elapsed() << "ms";
            resolving_ = false;
            emit resolved(result.location);
            co_return;
        }
        if (result.error.kind == GeoErrorKind::RateLimited) {
            retryAt_[index] = QDateTime::currentDateTimeUtc().addSecs(qMax(1, result.error.retryAfterSeconds));
        }
        qCDebug(lcGeo) << fetcher->name() << "failed after" << elapsed.elapsed()
                       << "ms:" << errorName(result.error.kind) << "HTTP" << result.error.httpStatus << "network error"
                       << result.error.networkError << "retry after" << result.error.retryAfterSeconds << "seconds";
    }
    if (active_ && generation == generation_) {
        resolving_ = false;
        qCDebug(lcGeo) << "all providers failed";
        emit resolved(std::nullopt);
    }
}

void GeoLocator::checkNetwork()
{
    if (!active_)
        return;
    const QString current = geoNetworkSignature();
    if (current == signature_)
        return;
    signature_ = current;
    qCDebug(lcGeo) << "network interfaces changed; scheduling new lookup";
    debounceTimer_.start();
}

} // namespace App
