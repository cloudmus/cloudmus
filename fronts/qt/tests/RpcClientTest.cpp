#include <QFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

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
    // Android unpacks its backends as it runs, and can only start its Python
    // from the native library directory.
    void manifestsCanComeFromADirectoryNamedByTheEnvironment()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("unpacked.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"id": "unpacked", "name": "Unpacked", "argv": ["python3", "-m", "backend"]})");
        file.close();
        qputenv("CLOUDMUS_BACKENDS_DIR", dir.path().toUtf8());
        qputenv("CLOUDMUS_BACKENDS_PYTHON", "/lib/libpython.so");
        const QList<Rpc::BackendManifest> manifests = Rpc::discoverManifests();
        qunsetenv("CLOUDMUS_BACKENDS_DIR");
        qunsetenv("CLOUDMUS_BACKENDS_PYTHON");
        const auto found = std::find_if(manifests.begin(), manifests.end(),
            [](const Rpc::BackendManifest& manifest) { return manifest.id == QStringLiteral("unpacked"); });
        QVERIFY(found != manifests.end());
        QCOMPARE(found->argv, QStringList({ QStringLiteral("/lib/libpython.so"), QStringLiteral("-m"), QStringLiteral("backend") }));
    }

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

    // As at a quit that skips shutdown(): ~QProcess kills the backend and
    // reports it while the client is half destroyed.
    void destroyingAClientWithItsBackendRunningIsSafe()
    {
        auto client = std::make_unique<Rpc::RpcClient>(fakeManifest());
        await(client->start(QProcessEnvironment::systemEnvironment()));
        bool unavailable = false;
        connect(client.get(), &Rpc::RpcClient::becameUnavailable, this, [&unavailable]() { unavailable = true; });
        client.reset();
        QVERIFY(!unavailable);
    }
};

QObject* makeRpcClientTest() { return new RpcClientTest; }

} // namespace Tests

#include "RpcClientTest.moc"
