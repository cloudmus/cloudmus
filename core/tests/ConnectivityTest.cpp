#include <QSignalSpy>
#include <QTest>

#include "Connectivity.h"
#include "TestSupport.h"

namespace Tests {

namespace {
using App::Connectivity;

// What each source answers to connection.check, and how often it's asked.
struct FakeService {
    QHash<QString, bool> online;
    QHash<QString, int> asked;
    // Sources without connectionCheck.
    QSet<QString> unsupported;
};

Rpc::Task<std::optional<Connectivity::CheckResult>> answer(FakeService* service, QString sourceId)
{
    ++service->asked[sourceId];
    if (service->unsupported.contains(sourceId))
        co_return std::nullopt;
    const bool online = service->online.value(sourceId, true);
    co_return Connectivity::CheckResult { online, online ? QString() : QStringLiteral("no route") };
}

std::unique_ptr<Connectivity> make(FakeService& service, int periodicMs = 20, int suspectMs = 10)
{
    auto connectivity = std::make_unique<Connectivity>(Connectivity::SystemNetwork::Ignore);
    connectivity->setIntervals(periodicMs, suspectMs);
    connectivity->setChecker([&service](QString sourceId) { return answer(&service, std::move(sourceId)); });
    connectivity->setSourcesProvider([]() { return QStringList { QStringLiteral("a"), QStringLiteral("b") }; });
    return connectivity;
}
}

class ConnectivityTest : public QObject {
    Q_OBJECT

private slots:
    void aSourceSayingSoIsOfflineUntilACheckFindsItBack()
    {
        FakeService service;
        auto connectivity = make(service);
        QSignalSpy changed(connectivity.get(), &Connectivity::offlineChanged);
        QSignalSpy online(connectivity.get(), &Connectivity::sourceOnline);

        service.online[QStringLiteral("a")] = false;
        connectivity->reportOffline(QStringLiteral("a"), QStringLiteral("DNS"));
        QVERIFY(connectivity->offline());
        QVERIFY(connectivity->isOffline(QStringLiteral("a")));
        QVERIFY(!connectivity->isOffline(QStringLiteral("b")));
        QCOMPARE(connectivity->offlineSources(), QStringList { QStringLiteral("a") });
        QVERIFY(connectivity->offlineSince(QStringLiteral("a")).isValid());
        QCOMPARE(changed.size(), 1);

        // The periodic check asks again, and finds it still gone.
        QTRY_VERIFY(service.asked.value(QStringLiteral("a")) >= 2);
        QVERIFY(connectivity->isOffline(QStringLiteral("a")));
        QCOMPARE(connectivity->offlineDetail(QStringLiteral("a")), QStringLiteral("no route"));

        service.online[QStringLiteral("a")] = true;
        QTRY_VERIFY(!connectivity->offline());
        QCOMPARE(changed.size(), 2);
        QCOMPARE(online.size(), 1);
        QCOMPARE(online.first().first().toString(), QStringLiteral("a"));
    }

    void aQuietDropShowsUpAtTheNextCheck()
    {
        FakeService service;
        auto connectivity = make(service);
        QVERIFY(!connectivity->offline());
        service.online[QStringLiteral("b")] = false;
        QTRY_VERIFY(connectivity->isOffline(QStringLiteral("b")));
    }

    void noNetworkOnTheSystemWinsAndStopsTheChecks()
    {
        FakeService service;
        auto connectivity = make(service);
        QSignalSpy online(connectivity.get(), &Connectivity::sourceOnline);
        connectivity->setSystemOffline(true);
        QVERIFY(connectivity->offline());
        QVERIFY(connectivity->isOffline(QStringLiteral("a")));
        // A source answering doesn't overrule it.
        connectivity->reportReached(QStringLiteral("a"));
        QVERIFY(connectivity->isOffline(QStringLiteral("a")));
        QCOMPARE(online.size(), 0);

        service.asked.clear();
        connectivity->checkNow();
        QTest::qWait(80);
        QCOMPARE(service.asked.size(), 0);
        QVERIFY(!await(connectivity->checkSource(QStringLiteral("a"))));

        // Back: each source is checked, and those found there come back.
        connectivity->setSystemOffline(false);
        QTRY_COMPARE(online.size(), 2);
        QVERIFY(!connectivity->offline());
    }

    void checkNowAsksEverySourceAtOnce()
    {
        FakeService service;
        auto connectivity = make(service, 60000);
        QSignalSpy checking(connectivity.get(), &Connectivity::checkingChanged);
        connectivity->checkNow();
        QTRY_COMPARE(service.asked.value(QStringLiteral("a")), 1);
        QCOMPARE(service.asked.value(QStringLiteral("b")), 1);
        QVERIFY(!connectivity->checking());
    }

    void anAwaitedCheckSaysWhetherTheSourceIsThere()
    {
        FakeService service;
        auto connectivity = make(service, 60000);
        QVERIFY(await(connectivity->checkSource(QStringLiteral("a"))));
        service.online[QStringLiteral("a")] = false;
        QVERIFY(!await(connectivity->checkSource(QStringLiteral("a"))));
        QVERIFY(connectivity->isOffline(QStringLiteral("a")));
    }

    void aSourceThatCantCheckIsAskedTheOldWay()
    {
        FakeService service;
        service.unsupported.insert(QStringLiteral("a"));
        auto connectivity = make(service, 60000);
        QSignalSpy retry(connectivity.get(), &Connectivity::retryWanted);
        connectivity->suspect(QStringLiteral("a"));
        QVERIFY(!connectivity->offline()); // not claimed
        QTRY_COMPARE(retry.size(), 1);
        QCOMPARE(retry.first().first().toString(), QStringLiteral("a"));
    }

    void pausedItChecksNothingUntilWoken()
    {
        FakeService service;
        auto connectivity = make(service);
        connectivity->setPaused(true);
        service.asked.clear();
        QTest::qWait(80);
        QCOMPARE(service.asked.size(), 0);
        connectivity->setPaused(false);
        QTRY_VERIFY(service.asked.value(QStringLiteral("a")) >= 1);
    }
};

QObject* makeConnectivityTest() { return new ConnectivityTest; }

} // namespace Tests

#include "ConnectivityTest.moc"
