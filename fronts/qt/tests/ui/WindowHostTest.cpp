#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTreeView>

#include "Core.h"
#include "FakeBackend.h"
#include "MainWindow.h"
#include "PlaylistSheet.h"
#include "RpcClient.h"
#include "TestSupport.h"
#include "TrackListModel.h"
#include "WindowHost.h"

namespace Tests {

class WindowHostTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        installFakeBackendManifest();
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/cloudmus/fronts/qt/config.ini"));
    }

    void aRecreatedWindowShowsWhatTheOldOneDid()
    {
        App::Core core;
        Ui::WindowHost host(core);
        host.show();
        QSignalSpy loaded(&core.sources(), &ViewModel::Sources::playlistsLoaded);
        core.sourceManager().startAll();
        QVERIFY(loaded.wait(10000));

        // Something open in the sheet, with its tracks listed.
        const Playlist first = core.sources().playlists(QStringLiteral("fake")).first();
        core.browse().openPlaylist(QStringLiteral("fake"), first);
        QTRY_COMPARE(core.browse().rows().size(), 2);

        // (Not comparing pointers: the new window may well be allocated
        // where the old one was.)
        const QPointer<Ui::MainWindow> before = host.window();
        QSignalSpy created(&host, &Ui::WindowHost::windowCreated);
        host.recreate();
        QVERIFY(created.wait(5000));
        QVERIFY(before.isNull());
        Ui::MainWindow* after = host.window();
        QVERIFY(after->isVisible());

        // The sidebar lists the source, the sheet is open on the same
        // playlist with the same tracks.
        auto* sidebar = after->findChild<QTreeView*>(QStringLiteral("sidebarView"));
        QVERIFY(sidebar != nullptr);
        QVERIFY(sidebar->model()->rowCount() >= 2); // History + the source
        auto* sheet = after->findChild<Ui::PlaylistSheet*>();
        QVERIFY(sheet != nullptr);
        QVERIFY(sheet->isPresented());
        QCOMPARE(sheet->trackModel()->rowCount(), 2);

        for (Rpc::RpcClient* client : core.sourceManager().clients())
            await(client->shutdown());
    }
};

QObject* makeWindowHostTest() { return new WindowHostTest; }

} // namespace Tests

#include "WindowHostTest.moc"
