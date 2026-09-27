#pragma once

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <optional>
#include <vector>

#include "Coro.h"

class QNetworkReply;

namespace App {

struct GeoLocation {
    QString countryId; // ISO 3166-1 alpha-2
    QString city;
    QString regionId; // ISO 3166-2, e.g. "RU-ROS"
    QString continentId; // UN M49, e.g. "150"

    bool isValid() const;
    bool operator==(const GeoLocation&) const = default;
};

enum class GeoErrorKind {
    Timeout,
    Network,
    Http,
    RateLimited,
    InvalidResponse,
    Rejected,
};

struct GeoError {
    GeoErrorKind kind = GeoErrorKind::InvalidResponse;
    int httpStatus = 0;
    int retryAfterSeconds = 0;
    int networkError = 0;
};

struct GeoFetchResult {
    std::optional<GeoLocation> location;
    GeoError error;
};

struct GeoHttpResponse {
    QByteArray body;
    QByteArray retryAfter;
    QByteArray rateLimitTtl;
    int httpStatus = 0;
    int networkError = 0;
    bool timedOut = false;
};

class GeoHttpAwaiter;

// One request at a time; abort() resumes the suspended coroutine so it can
// observe cancellation and release its frame before the owner is destroyed.
class GeoHttpClient {
public:
    explicit GeoHttpClient(QNetworkAccessManager& network)
        : network_(network)
    {
    }

    Rpc::Task<GeoHttpResponse> get(const QUrl& url);
    void abort();

private:
    friend class GeoHttpAwaiter;
    QNetworkAccessManager& network_;
    QPointer<QNetworkReply> activeReply_;
};

class GeoFetcher {
public:
    virtual ~GeoFetcher() = default;
    virtual QString name() const = 0;
    virtual Rpc::Task<GeoFetchResult> fetch(GeoHttpClient& http) = 0;
};

class IpWhoIsFetcher final : public GeoFetcher {
public:
    QString name() const override;
    Rpc::Task<GeoFetchResult> fetch(GeoHttpClient& http) override;
};

class IpApiFetcher final : public GeoFetcher {
public:
    QString name() const override;
    Rpc::Task<GeoFetchResult> fetch(GeoHttpClient& http) override;
};

class GeoLocator : public QObject {
    Q_OBJECT

public:
    explicit GeoLocator(QNetworkAccessManager& network, QObject* parent = nullptr,
        std::vector<std::unique_ptr<GeoFetcher>> fetchers = { });
    ~GeoLocator() override;

    void start();
    void stop();
    void refresh();
    bool resolving() const { return resolving_; }

signals:
    void resolved(std::optional<App::GeoLocation> location);

private:
    Rpc::Task<void> resolve(quint64 generation);
    void checkNetwork();

    GeoHttpClient http_;
    std::vector<std::unique_ptr<GeoFetcher>> fetchers_;
    std::vector<QDateTime> retryAt_;
    QTimer pollTimer_;
    QTimer debounceTimer_;
    QString signature_;
    quint64 generation_ = 0;
    bool active_ = false;
    bool resolving_ = false;
    bool networkSignalsConnected_ = false;
};

} // namespace App
