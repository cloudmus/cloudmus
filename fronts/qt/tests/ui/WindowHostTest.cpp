#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTreeView>

#include "Core.h"
#include "DownloadsPanel.h"
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

    void theDownloadsPanelListsWhatsUnderWay()
    {
        App::Core core;
        core.settings().setDownloadsEnabled(true);
        core.settings().setDownloadDirectory(QDir::temp().filePath(QStringLiteral("cloudmus-tests-downloads")));
        Ui::WindowHost host(core);
        QSignalSpy loaded(&core.sources(), &ViewModel::Sources::playlistsLoaded);
        core.sourceManager().startAll();
        QVERIFY(loaded.wait(10000));

        // A track that hangs halfway, and a playlist queued behind it.
        Track slow;
        slow.id = QStringLiteral("slow");
        slow.title = QStringLiteral("A Track That Takes Its Time");
        core.downloads().downloadTrack(QStringLiteral("fake"), slow);
        core.downloads().downloadPlaylist(
            QStringLiteral("fake"), core.sources().playlists(QStringLiteral("fake")).first());
        QTRY_COMPARE(core.downloads().jobs().first().received, qint64(50));

        auto* panel = new Ui::DownloadsPanel(core.downloads(), core.nowPlaying());
        panel->popup(QPoint(400, 600));
        QTRY_VERIFY(panel->isVisible());
        QVERIFY(panel->height() > 100);
        // A look at it, for whoever runs this with CLOUDMUS_TEST_SNAPSHOTS
        // set to a folder.
        const QString snapshots = qEnvironmentVariable("CLOUDMUS_TEST_SNAPSHOTS");
        if (!snapshots.isEmpty())
            panel->grab().save(QDir(snapshots).filePath(QStringLiteral("downloads-panel.png")));
        panel->close();

        core.downloads().cancel(core.downloads().jobs().first().id);
        QTRY_VERIFY(!core.downloads().isActive());
        core.settings().setDownloadsEnabled(false);
        for (Rpc::RpcClient* client : core.sourceManager().clients())
            await(client->shutdown());
    }
};

QObject* makeWindowHostTest() { return new WindowHostTest; }

} // namespace Tests

#include "WindowHostTest.moc"
