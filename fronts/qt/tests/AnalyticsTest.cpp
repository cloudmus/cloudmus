#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include "Analytics.h"
#include "AuthStates.h"
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
            setError(QNetworkReply::HostNotFoundError, QStringLiteral("request rejected"));
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
        QByteArray userAgent;

        QString param(const QString& name) const { return QUrlQuery(url).queryItemValue(name, QUrl::FullyDecoded); }
        bool hasParam(const QString& name) const { return QUrlQuery(url).hasQueryItem(name); }
        QList<QUrlQuery> bodyEvents() const
        {
            QList<QUrlQuery> events;
            for (const QByteArray& line : body.split('\n'))
                if (!line.trimmed().isEmpty())
                    events.append(QUrlQuery(QString::fromUtf8(line.trimmed())));
            return events;
        }
    };
    QList<Request> requests;
    bool autoFinish = true;
    bool fail = false;
    bool aborted = false;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* data) override
    {
        if (operation == PostOperation)
            requests.append({ request.url(), data->readAll(), request.rawHeader("User-Agent") });
        return new FakeReply(request, this, &aborted, autoFinish, fail);
    }
};

class AnalyticsTest : public QObject {
    Q_OBJECT
private slots:
    void picksPreferredSystemUiLanguage()
    {
        QCOMPARE(App::languageForAnalytics({ QStringLiteral("ru-Cyrl-RU"), QStringLiteral("en-US") }),
            QStringLiteral("ru-ru"));
        QCOMPARE(
            App::languageForAnalytics({ QStringLiteral("ru-RU"), QStringLiteral("en-US") }), QStringLiteral("ru-ru"));
        QCOMPARE(App::languageForAnalytics({ QStringLiteral("en-Latn-US") }), QStringLiteral("en-us"));
        QCOMPARE(App::languageForAnalytics({ QStringLiteral("pt_BR") }), QStringLiteral("pt-br"));
        QVERIFY(App::languageForAnalytics({ QStringLiteral("und-Latn") }).isEmpty());
        QVERIFY(App::languageForAnalytics({ QStringLiteral("C") }).isEmpty());
        QVERIFY(App::languageForAnalytics({ }).isEmpty());
    }

    void clientIdHasTheWebTagShape()
    {
        Config::Settings settings;
        const QString id = settings.analyticsClientId();
        QVERIFY(QRegularExpression(QStringLiteral("^\\d+\\.\\d+$")).match(id).hasMatch());
        QCOMPARE(settings.analyticsClientId(), id);
    }

    void sendsOnlyApprovedData()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        analytics.recordPlayback(QStringLiteral("personal-source-id"));
        analytics.recordSourceOpened(QStringLiteral("youtube-music"));
        analytics.recordDownload(QStringLiteral("local-folder"), true, 3);
        analytics.recordPlaylistChange(QStringLiteral("yandex-music"), false);
        analytics.recordStarPrompt(App::Analytics::StarPromptAction::Star);
        QTRY_COMPARE(network.requests.size(), 1);
        const auto& request = network.requests.first();
        QCOMPARE(request.url.path(), QStringLiteral("/g/collect"));
        QCOMPARE(request.param(QStringLiteral("v")), QStringLiteral("2"));
        QCOMPARE(request.param(QStringLiteral("tid")), QStringLiteral("G-TEST123"));
        QCOMPARE(request.param(QStringLiteral("cid")), settings.analyticsClientId());
        QCOMPARE(request.param(QStringLiteral("_ss")), QStringLiteral("1"));
        QCOMPARE(request.param(QStringLiteral("npa")), QStringLiteral("1"));
        QVERIFY(!request.hasParam(QStringLiteral("_dbg")));
        QVERIFY(request.userAgent.startsWith("Mozilla/5.0 ("));
        QVERIFY(request.userAgent.endsWith(") CloudMus/1.2.3"));

        const QList<QUrlQuery> events = request.bodyEvents();
        QCOMPARE(events.size(), 6);
        QCOMPARE(events[0].queryItemValue(QStringLiteral("en")), QStringLiteral("app_launch"));
        QCOMPARE(events[0].queryItemValue(QStringLiteral("ep.app_version")), QStringLiteral("1.2.3"));
        QCOMPARE(events[1].queryItemValue(QStringLiteral("ep.source")), QStringLiteral("other"));
        QCOMPARE(events[2].queryItemValue(QStringLiteral("ep.source")), QStringLiteral("youtube_music"));
        QCOMPARE(events[3].queryItemValue(QStringLiteral("ep.kind")), QStringLiteral("playlist"));
        QCOMPARE(events[3].queryItemValue(QStringLiteral("epn.saved_count")), QStringLiteral("3"));
        QCOMPARE(events[4].queryItemValue(QStringLiteral("ep.action")), QStringLiteral("remove"));
        QCOMPARE(events[5].queryItemValue(QStringLiteral("en")), QStringLiteral("star_prompt"));
        QCOMPARE(events[5].queryItemValue(QStringLiteral("ep.action")), QStringLiteral("star"));
        QVERIFY(!request.body.contains("personal-source-id"));
        QVERIFY(!request.url.toString().contains(QStringLiteral("personal-source-id")));
    }

    void sendsHealthAndUsageEvents()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("1.2.3"));
        App::Analytics::LaunchInfo launch;
        launch.atLogin = true;
        launch.theme = QStringLiteral("dark");
        launch.uiLanguage = QStringLiteral("system");
        analytics.recordLaunch(launch);
        analytics.recordCrashes(0); // nothing to say
        analytics.recordCrashes(2);
        analytics.recordPlaybackFailure(QStringLiteral("youtube-music"), QStringLiteral("timeout"));
        analytics.recordBackendFailure(QStringLiteral("yandex-music"), true);
        analytics.recordSignIn(QStringLiteral("yandex-music"), App::Analytics::SignInStep::Success);
        analytics.recordUpdate(QStringLiteral("offered"), true);
        analytics.recordTrackFeedback(QStringLiteral("local-folder"), QStringLiteral("dislike"));
        analytics.recordControlUsed(QStringLiteral("tray"));
        analytics.recordControlUsed(QStringLiteral("tray")); // once a run
        analytics.recordListeningTime(0); // nothing to say
        analytics.recordListeningTime(42);
        QTRY_COMPARE(network.requests.size(), 1);

        const QList<QUrlQuery> events = network.requests.first().bodyEvents();
        const auto value = [&events](int i, const char* key) { return events[i].queryItemValue(QLatin1String(key)); };
        QCOMPARE(events.size(), 9);
        QCOMPARE(value(0, "ep.at_login"), QStringLiteral("1"));
        QCOMPARE(value(0, "ep.hidden"), QStringLiteral("0"));
        QCOMPARE(value(0, "ep.theme"), QStringLiteral("dark"));
        QCOMPARE(value(0, "ep.ui_language"), QStringLiteral("system"));
        QCOMPARE(value(1, "en"), QStringLiteral("app_crashed"));
        QCOMPARE(value(1, "epn.count"), QStringLiteral("2"));
        QCOMPARE(value(2, "en"), QStringLiteral("playback_failed"));
        QCOMPARE(value(2, "ep.reason"), QStringLiteral("timeout"));
        QCOMPARE(value(3, "ep.reason"), QStringLiteral("gave_up"));
        QCOMPARE(value(4, "ep.action"), QStringLiteral("success"));
        QCOMPARE(value(5, "ep.kind"), QStringLiteral("manual"));
        QCOMPARE(value(6, "ep.action"), QStringLiteral("dislike"));
        QCOMPARE(value(7, "ep.trigger"), QStringLiteral("tray"));
        QCOMPARE(value(8, "epn.minutes"), QStringLiteral("42"));
    }

    // Only a sign-in the user went through counts — not being signed in
    // already at startup.
    void authStatesTellTheStepsOfASignIn()
    {
        Rpc::AuthStates states;
        QSignalSpy prompted(&states, &Rpc::AuthStates::signInPrompted);
        QSignalSpy completed(&states, &Rpc::AuthStates::signInCompleted);
        QSignalSpy failed(&states, &Rpc::AuthStates::signInFailed);
        const QString source = QStringLiteral("yandex-music");

        states.setAuthenticated(source);
        QCOMPARE(completed.size(), 0);

        states.setPrompt(source, { });
        states.setPrompt(source, { }); // a refreshed code, the same sign-in
        QCOMPARE(prompted.size(), 1);
        states.setAuthenticated(source);
        QCOMPARE(completed.size(), 1);

        states.setPrompt(source, { });
        states.setError(source, QStringLiteral("denied"));
        QCOMPARE(prompted.size(), 2);
        QCOMPARE(failed.size(), 1);
        states.setAuthenticated(source);
        QCOMPARE(completed.size(), 1);
    }

    void loneEventGoesInTheUrlAndSessionStartsOnce()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.setDebugView(true);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        QTRY_COMPARE(network.requests.size(), 1);
        analytics.recordPlayback(QStringLiteral("local-folder"));
        QTRY_COMPARE(network.requests.size(), 2);

        const auto& first = network.requests[0];
        QVERIFY(first.body.isEmpty());
        QCOMPARE(first.param(QStringLiteral("en")), QStringLiteral("app_launch"));
        QCOMPARE(first.param(QStringLiteral("_s")), QStringLiteral("1"));
        QCOMPARE(first.param(QStringLiteral("_ss")), QStringLiteral("1"));
        QCOMPARE(first.param(QStringLiteral("_dbg")), QStringLiteral("1"));

        const auto& second = network.requests[1];
        QCOMPARE(second.param(QStringLiteral("en")), QStringLiteral("playback_started"));
        QCOMPARE(second.param(QStringLiteral("ep.source")), QStringLiteral("local_folder"));
        QCOMPARE(second.param(QStringLiteral("_s")), QStringLiteral("2"));
        QCOMPARE(second.param(QStringLiteral("sid")), first.param(QStringLiteral("sid")));
        QCOMPARE(second.param(QStringLiteral("sct")), first.param(QStringLiteral("sct")));
        QVERIFY(!second.hasParam(QStringLiteral("_ss")));
        QVERIFY(!second.hasParam(QStringLiteral("_fv")));
    }

    void countsSessionsAcrossRuns()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        App::Analytics firstRun(settings, nullptr, &network);
        firstRun.configure(QStringLiteral("G-TEST123"), QStringLiteral("1.2.3"));
        firstRun.recordLaunch();
        QTRY_COMPARE(network.requests.size(), 1);
        App::Analytics secondRun(settings, nullptr, &network);
        secondRun.configure(QStringLiteral("G-TEST123"), QStringLiteral("1.2.3"));
        secondRun.recordLaunch();
        QTRY_COMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests[1].param(QStringLiteral("sct")).toInt(),
            network.requests[0].param(QStringLiteral("sct")).toInt() + 1);
        QCOMPARE(network.requests[1].param(QStringLiteral("_ss")), QStringLiteral("1"));
        QVERIFY(!network.requests[1].hasParam(QStringLiteral("_fv")));
    }

    void disablingDropsPendingEventsAndKeepsId()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        const QString id = settings.analyticsClientId();
        FakeNetwork network;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("1.2.3"));
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
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        QTRY_COMPARE(network.requests.size(), 1);
        analytics.recordPlayback(QStringLiteral("local-folder"));
        analytics.setEnabled(false);
        QVERIFY(network.aborted);
        QTest::qWait(50);
        QCOMPARE(network.requests.size(), 1);
    }

    void debugLogShowsPayloadAndOutcomeWithoutClientId()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        const QString clientId = settings.analyticsClientId();
        FakeNetwork network;
        App::Analytics analytics(settings, nullptr, &network);
        QStringList messages;
        capturedAnalyticsMessages = &messages;
        const QtMessageHandler previous = qInstallMessageHandler(captureAnalyticsMessage);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("1.2.3"));
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
        QVERIFY(!log.contains(clientId));
    }

    void flushSendsQueuedEvents()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        bool flushed = false;
        analytics.flush(2000, [&flushed]() { flushed = true; });
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(!flushed);
        QTRY_VERIFY(flushed);

        bool idleFlushed = false;
        analytics.flush(2000, [&idleFlushed]() { idleFlushed = true; });
        QVERIFY(idleFlushed);
    }

    void flushGivesUpAfterTimeout()
    {
        Config::Settings settings;
        settings.setAnalyticsEnabled(true);
        FakeNetwork network;
        network.autoFinish = false;
        App::Analytics analytics(settings, nullptr, &network);
        analytics.configure(QStringLiteral("G-TEST123"), QStringLiteral("1.2.3"));
        analytics.recordLaunch();
        QTRY_COMPARE(network.requests.size(), 1);
        bool flushed = false;
        analytics.flush(50, [&flushed]() { flushed = true; });
        QVERIFY(!flushed);
        QTRY_VERIFY(flushed);
    }
};
} // namespace

QObject* makeAnalyticsTest() { return new AnalyticsTest; }

} // namespace Tests

#include "AnalyticsTest.moc"
