#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <cstring>
#include <utility>

#include "Analytics.h"
#include "Settings.h"

namespace Tests {

namespace {
QStringList* capturedAnalyticsMessages = nullptr;

void captureAnalyticsMessage(QtMsgType, const QMessageLogContext& context, const QString& message)
{
    if (capturedAnalyticsMessages && QByteArray(context.category) == "cloudmus.app.analytics")
        capturedAnalyticsMessages->append(message);
}

struct ReplyPlan {
    int status = 204;
    QByteArray body;
    QByteArray retryAfter;
    QByteArray rateLimitTtl;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    bool autoFinish = true;
};

class FakeReply : public QNetworkReply {
public:
    explicit FakeReply(const QNetworkRequest& request, QObject* parent, bool* aborted, ReplyPlan plan)
        : QNetworkReply(parent)
        , aborted_(aborted)
        , body_(std::move(plan.body))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, plan.status);
        if (!plan.retryAfter.isEmpty())
            setRawHeader("Retry-After", plan.retryAfter);
        if (!plan.rateLimitTtl.isEmpty())
            setRawHeader("X-Ttl", plan.rateLimitTtl);
        if (plan.error != QNetworkReply::NoError)
            setError(QNetworkReply::HostNotFoundError, QStringLiteral("request rejected testsecret"));
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (plan.autoFinish)
            QTimer::singleShot(0, this, [this]() {
                if (!isFinished()) {
                    setFinished(true);
                    emit finished();
                }
            });
    }

    void abort() override
    {
        if (isFinished())
            return;
        *aborted_ = true;
        setFinished(true);
        emit finished();
    }

protected:
    qint64 readData(char* output, qint64 size) override
    {
        if (body_.isEmpty())
            return -1;
        const qint64 count = qMin(size, qint64(body_.size()));
        memcpy(output, body_.constData(), size_t(count));
        body_.remove(0, count);
        return count;
    }

private:
    bool* aborted_;
    QByteArray body_;
};

class FakeNetwork : public QNetworkAccessManager {
public:
    struct Request {
        QUrl url;
        QByteArray body;
        QByteArray userAgent;
    };
    QList<Request> requests;
    QList<QUrl> geoRequests;
    QList<ReplyPlan> geoReplies;
    bool autoFinish = true;
    bool fail = false;
    bool aborted = false;
    bool geoAborted = false;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* data) override
    {
        if (operation == GetOperation) {
            geoRequests.append(request.url());
            const ReplyPlan plan = geoReplies.isEmpty() ? ReplyPlan { } : geoReplies.takeFirst();
            return new FakeReply(request, this, &geoAborted, plan);
        }
        if (operation == PostOperation)
            requests.append({ request.url(), data->readAll(), request.rawHeader("User-Agent") });
        ReplyPlan plan;
        plan.autoFinish = autoFinish;
        plan.status = fail ? 0 : 204;
        plan.error = fail ? QNetworkReply::HostNotFoundError : QNetworkReply::NoError;
        return new FakeReply(request, this, &aborted, plan);
    }
};

class AnalyticsTest : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        Config::Settings settings;
        settings.setAnalyticsLocation({ }, { }, { }, { });
    }

    void picksPreferredSystemUiLanguage()
    {
        QCOMPARE(App::languageForAnalytics({ QStringLiteral("ru-Cyrl-RU"), QStringLiteral("en-US") }),
            QStringLiteral("ru-RU"));
        QCOMPARE(
            App::languageForAnalytics({ QStringLiteral("ru-RU"), QStringLiteral("en-US") }), QStringLiteral("ru-RU"));
        QCOMPARE(App::languageForAnalytics({ QStringLiteral("en-Latn-US") }), QStringLiteral("en-US"));
        QCOMPARE(App::languageForAnalytics({ QStringLiteral("pt_BR") }), QStringLiteral("pt-BR"));
        QVERIFY(App::languageForAnalytics({ QStringLiteral("und-Latn") }).isEmpty());
        QVERIFY(App::languageForAnalytics({ QStringLiteral("C") }).isEmpty());
        QVERIFY(App::languageForAnalytics({ }).isEmpty());
    }

    void sendsOnlyApprovedData()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("testsecret"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        analytics.recordPlayback(QStringLiteral("personal-source-id"));
        analytics.recordSourceOpened(QStringLiteral("youtube-music"));
        analytics.recordDownload(QStringLiteral("local-folder"), true, 3);
        analytics.recordPlaylistChange(QStringLiteral("yandex-music"), false);
        QTRY_COMPARE(network.requests.size(), 1);
        const auto& request = network.requests.first();
        QCOMPARE(request.url.path(), QStringLiteral("/mp/collect"));
        QCOMPARE(request.url.query(), QStringLiteral("measurement_id=G-TEST123&api_secret=testsecret"));
        QVERIFY(request.userAgent.isEmpty());
        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value(QStringLiteral("client_id")).toString(), settings.analyticsClientId());
        const QJsonArray events = body.value(QStringLiteral("events")).toArray();
        QCOMPARE(events.size(), 5);
        QCOMPARE(events[0].toObject().value(QStringLiteral("name")).toString(), QStringLiteral("app_launch"));
        QCOMPARE(
            events[1].toObject().value(QStringLiteral("params")).toObject().value(QStringLiteral("source")).toString(),
            QStringLiteral("other"));
        QCOMPARE(
            events[2].toObject().value(QStringLiteral("params")).toObject().value(QStringLiteral("source")).toString(),
            QStringLiteral("youtube_music"));
        QCOMPARE(events[3]
                     .toObject()
                     .value(QStringLiteral("params"))
                     .toObject()
                     .value(QStringLiteral("saved_count"))
                     .toInt(),
            3);
        QCOMPARE(
            events[4].toObject().value(QStringLiteral("params")).toObject().value(QStringLiteral("action")).toString(),
            QStringLiteral("remove"));
        QVERIFY(!request.body.contains("personal-source-id"));
        QCOMPARE(
            body.value(QStringLiteral("consent")).toObject().value(QStringLiteral("ad_personalization")).toString(),
            QStringLiteral("DENIED"));
    }

    void disablingDropsPendingEventsAndKeepsId()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        const QString id = settings.analyticsClientId();
        FakeNetwork network;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("testsecret"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        analytics.setEnabled(false);
        QTest::qWait(50);
        QCOMPARE(network.requests.size(), 0);
        analytics.recordPlayback(QStringLiteral("local-folder"));
        QTest::qWait(50);
        QCOMPARE(network.requests.size(), 0);
        analytics.setEnabled(true);
        QCOMPARE(settings.analyticsClientId(), id);
        analytics.recordLaunch();
        QTRY_COMPARE(network.requests.size(), 1);
    }

    void disablingAbortsActiveRequest()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        network.autoFinish = false;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("testsecret"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        QTRY_COMPARE(network.requests.size(), 1);
        analytics.recordPlayback(QStringLiteral("local-folder"));
        analytics.setEnabled(false);
        QVERIFY(network.aborted);
        QTest::qWait(50);
        QCOMPARE(network.requests.size(), 1);
    }

    void debugLogShowsPayloadAndOutcomeWithoutCredentials()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        const QString clientId = settings.analyticsClientId();
        FakeNetwork network;
        App::Analytics analytics(settings, nullptr, &network);
        QStringList messages;
        capturedAnalyticsMessages = &messages;
        const QtMessageHandler previous = qInstallMessageHandler(captureAnalyticsMessage);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("testsecret"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        QTest::qWait(50);
        network.fail = true;
        analytics.recordSourceOpened(QStringLiteral("local-folder"));
        QTest::qWait(50);
        network.fail = false;
        network.autoFinish = false;
        analytics.recordPlayback(QStringLiteral("local-folder"));
        QTest::qWait(50);
        analytics.setEnabled(false);
        qInstallMessageHandler(previous);
        capturedAnalyticsMessages = nullptr;
        const QString log = messages.join(QLatin1Char('\n'));
        QVERIFY(log.contains(QStringLiteral("initialized")));
        QVERIFY(log.contains(QStringLiteral("app_launch")));
        QVERIFY(log.contains(QStringLiteral("source_opened")));
        QVERIFY(log.contains(QStringLiteral("delivered, HTTP 204")));
        QVERIFY(log.contains(QStringLiteral("failed, HTTP 0")));
        QVERIFY(log.contains(QStringLiteral("cancelled because collection was disabled")));
        QVERIFY(log.contains(QStringLiteral("<redacted>")));
        QVERIFY(!log.contains(QStringLiteral("testsecret")));
        QVERIFY(!log.contains(clientId));
    }

    void sendsFreshLocationAndCachesIt()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        network.geoReplies.append({ 200,
            R"({"success":true,"continent_code":"EU","country_code":"FR","region_code":"IDF","city":"Paris"})" });
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("testsecret"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        QTRY_COMPARE(network.requests.size(), 1);
        QCOMPARE(network.geoRequests.size(), 1);
        QCOMPARE(network.geoRequests.first().host(), QStringLiteral("ipwho.is"));
        const QJsonObject body = QJsonDocument::fromJson(network.requests.first().body).object();
        QCOMPARE(body.value(QStringLiteral("user_location")).toObject().value(QStringLiteral("country_id")).toString(),
            QStringLiteral("FR"));
        const QJsonObject location = body.value(QStringLiteral("user_location")).toObject();
        QCOMPARE(location.value(QStringLiteral("city")).toString(), QStringLiteral("Paris"));
        QCOMPARE(location.value(QStringLiteral("region_id")).toString(), QStringLiteral("FR-IDF"));
        QCOMPARE(location.value(QStringLiteral("continent_id")).toString(), QStringLiteral("150"));
        const QJsonObject device = body.value(QStringLiteral("device")).toObject();
        QCOMPARE(device.value(QStringLiteral("category")).toString(), QStringLiteral("desktop"));
        QCOMPARE(device.value(QStringLiteral("browser")).toString(), QStringLiteral("CloudMus"));
        QCOMPARE(device.value(QStringLiteral("browser_version")).toString(), QStringLiteral("1.2.3"));
        QCOMPARE(settings.analyticsCountryId(), QStringLiteral("FR"));
        QCOMPARE(settings.analyticsCity(), QStringLiteral("Paris"));
        QCOMPARE(settings.analyticsRegionId(), QStringLiteral("FR-IDF"));
        QCOMPARE(settings.analyticsContinentId(), QStringLiteral("150"));
        QVERIFY(!network.requests.first().body.contains("testsecret"));
    }

    void rateLimitFallsBackAndSkipsProviderUntilReset()
    {
        FakeNetwork network;
        network.geoReplies.append({ 429, { }, { }, "120" });
        network.geoReplies.append(
            { 200, R"({"status":"success","continentCode":"EU","countryCode":"DE","region":"BE","city":"Berlin"})" });
        network.geoReplies.append({ 200, R"({"status":"success","countryCode":"DE","city":"Hamburg"})" });
        App::GeoLocator locator(network);
        std::optional<App::GeoLocation> latest;
        int resolvedCount = 0;
        QObject::connect(&locator, &App::GeoLocator::resolved, &locator, [&](std::optional<App::GeoLocation> location) {
            latest = location;
            ++resolvedCount;
        });
        locator.start();
        QTRY_COMPARE(resolvedCount, 1);
        QVERIFY(latest);
        QCOMPARE(latest->city, QStringLiteral("Berlin"));
        QCOMPARE(latest->regionId, QStringLiteral("DE-BE"));
        QCOMPARE(latest->continentId, QStringLiteral("150"));
        QCOMPARE(network.geoRequests.size(), 2);
        QCOMPARE(network.geoRequests[1].host(), QStringLiteral("ip-api.com"));
        locator.refresh();
        QTRY_COMPARE(resolvedCount, 2);
        QCOMPARE(network.geoRequests.size(), 3);
        QCOMPARE(network.geoRequests[2].host(), QStringLiteral("ip-api.com"));
        QCOMPARE(latest->city, QStringLiteral("Hamburg"));
    }

    void networkErrorFallsBackToSecondProvider()
    {
        FakeNetwork network;
        ReplyPlan error;
        error.status = 0;
        error.error = QNetworkReply::HostNotFoundError;
        network.geoReplies.append(error);
        network.geoReplies.append({ 200, R"({"status":"success","countryCode":"GB","city":"London"})" });
        App::GeoLocator locator(network);
        std::optional<App::GeoLocation> result;
        QObject::connect(&locator, &App::GeoLocator::resolved, &locator,
            [&](std::optional<App::GeoLocation> location) { result = location; });
        locator.start();
        QTRY_VERIFY(result.has_value());
        QCOMPARE(network.geoRequests.size(), 2);
        QCOMPARE(result->countryId, QStringLiteral("GB"));
    }

    void failedProvidersUseCachedLocationOrOmitIt()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        settings.setAnalyticsLocation(QStringLiteral("NL"), QStringLiteral("Amsterdam"), { }, { });
        FakeNetwork network;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("testsecret"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        QTRY_COMPARE(network.requests.size(), 1);
        QCOMPARE(QJsonDocument::fromJson(network.requests.first().body)
                     .object()
                     .value(QStringLiteral("user_location"))
                     .toObject()
                     .value(QStringLiteral("country_id"))
                     .toString(),
            QStringLiteral("NL"));
        QCOMPARE(network.geoRequests.size(), 2);

        settings.setAnalyticsLocation({ }, { }, { }, { });
        FakeNetwork secondNetwork;
        App::Analytics second(settings, nullptr, &secondNetwork);
        second.configure(QStringLiteral("G-TEST123"), QStringLiteral("testsecret"), QStringLiteral("1.2.3"));
        second.recordLaunch();
        QTRY_COMPARE(secondNetwork.requests.size(), 1);
        QVERIFY(!QJsonDocument::fromJson(secondNetwork.requests.first().body)
                .object()
                .contains(QStringLiteral("user_location")));
    }

    void timeoutAndNetworkRefreshMoveToNextResult()
    {
        FakeNetwork network;
        ReplyPlan stalled;
        stalled.autoFinish = false;
        network.geoReplies.append(stalled);
        network.geoReplies.append({ 200, R"({"status":"success","countryCode":"IT","city":"Rome"})" });
        App::GeoLocator locator(network);
        std::optional<App::GeoLocation> latest;
        int resolvedCount = 0;
        QObject::connect(&locator, &App::GeoLocator::resolved, &locator, [&](std::optional<App::GeoLocation> location) {
            latest = location;
            ++resolvedCount;
        });
        locator.start();
        QTRY_COMPARE_WITH_TIMEOUT(resolvedCount, 1, 6000);
        QVERIFY(network.geoAborted);
        QCOMPARE(network.geoRequests.size(), 2);
        QVERIFY(latest);
        QCOMPARE(latest->countryId, QStringLiteral("IT"));

        network.geoReplies.append(stalled);
        network.geoReplies.append({ 200, R"({"success":true,"country_code":"ES","city":"Madrid"})" });
        locator.refresh();
        QTRY_COMPARE(network.geoRequests.size(), 3);
        locator.refresh();
        QTRY_COMPARE(resolvedCount, 2);
        QCOMPARE(latest->countryId, QStringLiteral("ES"));
    }

    void flushSendsWithoutWaitingForGeolocation()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        ReplyPlan stalled;
        stalled.autoFinish = false;
        network.geoReplies.append(stalled);
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("testsecret"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        QTest::qWait(50);
        QCOMPARE(network.requests.size(), 0);
        bool flushed = false;
        analytics.flush(2000, [&flushed]() { flushed = true; });
        QCOMPARE(network.requests.size(), 1);
        QTRY_VERIFY(flushed);

        bool idleFlushed = false;
        analytics.flush(2000, [&idleFlushed]() { idleFlushed = true; });
        QVERIFY(idleFlushed);
    }

    void flushGivesUpAfterTimeout()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        settings.setAnalyticsLocation(QStringLiteral("NL"), { }, { }, { });
        FakeNetwork network;
        network.autoFinish = false;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("testsecret"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        QTRY_COMPARE(network.requests.size(), 1);
        bool flushed = false;
        analytics.flush(50, [&flushed]() { flushed = true; });
        QVERIFY(!flushed);
        QTRY_VERIFY(flushed);
    }

    void disablingCancelsGeolocation()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        ReplyPlan stalled;
        stalled.autoFinish = false;
        network.geoReplies.append(stalled);
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("testsecret"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        QCOMPARE(network.geoRequests.size(), 1);
        analytics.setEnabled(false);
        QVERIFY(network.geoAborted);
        QTest::qWait(50);
        QCOMPARE(network.requests.size(), 0);
    }
};
} // namespace

QObject* makeAnalyticsTest() { return new AnalyticsTest; }

} // namespace Tests

#include "AnalyticsTest.moc"
