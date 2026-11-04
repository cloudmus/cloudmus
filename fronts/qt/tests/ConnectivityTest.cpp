#include <QSignalSpy>
#include <QTest>

#include "Connectivity.h"

namespace Tests {

class ConnectivityTest : public QObject {
    Q_OBJECT

private slots:
    void aSourceSayingSoIsOfflineUntilItAnswers()
    {
        App::Connectivity connectivity;
        connectivity.setRetryDelays(20, 40);
        // Whatever the machine running this says.
        connectivity.reportReached(QStringLiteral("other"));
        QVERIFY(!connectivity.offline());
        QSignalSpy changed(&connectivity, &App::Connectivity::offlineChanged);
        QSignalSpy retry(&connectivity, &App::Connectivity::retryWanted);

        connectivity.reportOffline(QStringLiteral("a"));
        QVERIFY(connectivity.offline());
        QCOMPARE(changed.size(), 1);
        QTRY_VERIFY(retry.size() >= 2); // and again, as long as it lasts
        QCOMPARE(retry.first().first().toString(), QStringLiteral("a"));

        connectivity.reportReached(QStringLiteral("a"));
        QVERIFY(!connectivity.offline());
        QCOMPARE(changed.size(), 2);
        retry.clear();
        QTest::qWait(100);
        QCOMPARE(retry.size(), 0); // nothing more to ask
    }

    void aFailureThatMayBeTheNetworkIsRetriedWithoutClaimingIt()
    {
        App::Connectivity connectivity;
        connectivity.setRetryDelays(20, 40);
        connectivity.reportReached(QStringLiteral("other"));
        QSignalSpy retry(&connectivity, &App::Connectivity::retryWanted);
        connectivity.suspect(QStringLiteral("a"));
        QVERIFY(!connectivity.offline());
        QTRY_VERIFY(retry.size() >= 1);
        connectivity.reportReached(QStringLiteral("a"));
    }
};

QObject* makeConnectivityTest() { return new ConnectivityTest; }

} // namespace Tests

#include "ConnectivityTest.moc"
