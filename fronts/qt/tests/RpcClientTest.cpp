#include <QProcessEnvironment>
#include <QTest>

#include "FakeBackend.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "TestSupport.h"

namespace Tests {

namespace {
Rpc::BackendManifest fakeManifest()
{
    Rpc::BackendManifest manifest;
    manifest.id = QStringLiteral("fake");
    manifest.name = QStringLiteral("Fake Source");
    manifest.argv = fakeBackendArgv();
    return manifest;
}
} // namespace

class RpcClientTest : public QObject {
    Q_OBJECT

private slots:
    void startReadsTheSourceFromInitialize()
    {
        Rpc::RpcClient client(fakeManifest());
        await(client.start(QProcessEnvironment::systemEnvironment()));
        QVERIFY(client.available());
        QCOMPARE(client.sourceId(), QStringLiteral("fake"));
        QCOMPARE(client.sourceName(), QStringLiteral("Fake Source"));
        QVERIFY(client.capabilities()
                .value(QStringLiteral("browse"))
                .toObject()
                .value(QStringLiteral("playlists"))
                .toBool());
        await(client.shutdown());
    }

    void aCallReturnsItsResultAndNotificationsArriveOnTheWay()
    {
        Rpc::RpcClient client(fakeManifest());
        QString notified;
        client.notifications.onError = [&notified](const ErrorParams& e) { notified = e.message; };
        await(client.start(QProcessEnvironment::systemEnvironment()));

        const ListPlaylistsResult result = await(Rpc::catalogListPlaylists(client));
        QCOMPARE(result.playlists.size(), 2);
        QCOMPARE(result.playlists.first().id, QStringLiteral("p1"));
        QCOMPARE(result.playlists.first().trackCount, 2);
        QCOMPARE(notified, QStringLiteral("heads up"));
        await(client.shutdown());
    }

    void anErrorReplyThrowsWithItsCode()
    {
        Rpc::RpcClient client(fakeManifest());
        await(client.start(QProcessEnvironment::systemEnvironment()));
        try {
            await(client.callRaw(QStringLiteral("no.such.method"), { }, 5000));
            QFAIL("expected an RpcCallException");
        } catch (const Rpc::RpcCallException& e) {
            QCOMPARE(e.error().code, -32601);
        }
        await(client.shutdown());
    }
};

QObject* makeRpcClientTest() { return new RpcClientTest; }

} // namespace Tests

#include "RpcClientTest.moc"
