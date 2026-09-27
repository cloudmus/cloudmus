#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTest>
#include <QTimer>

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

class FakeReply : public QNetworkReply {
public:
    explicit FakeReply(const QNetworkRequest& request, QObject* parent, bool* aborted, bool autoFinish, bool fail)
        : QNetworkReply(parent)
        , aborted_(aborted)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, fail ? 0 : 204);
        if (fail)
            setError(QNetworkReply::HostNotFoundError, QStringLiteral("request rejected testsecret"));
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (autoFinish)
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
    qint64 readData(char*, qint64) override { return -1; }

private:
    bool* aborted_;
};

class FakeNetwork : public QNetworkAccessManager {
public:
    struct Request {
        QUrl url;
        QByteArray body;
    };
    QList<Request> requests;
    bool autoFinish = true;
    bool fail = false;
    bool aborted = false;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* data) override
    {
        if (operation == PostOperation)
            requests.append({ request.url(), data->readAll() });
        return new FakeReply(request, this, &aborted, autoFinish, fail);
    }
};

class AnalyticsTest : public QObject {
    Q_OBJECT
private slots:
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
};
} // namespace

QObject* makeAnalyticsTest() { return new AnalyticsTest; }

} // namespace Tests

#include "AnalyticsTest.moc"
