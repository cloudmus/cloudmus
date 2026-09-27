#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QListView>
#include <QPointer>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QTest>
#include <QTreeView>

#include "Core.h"
#include "DownloadsPanel.h"
#include "FakeBackend.h"
#include "MainWindow.h"
#include "MprisService.h"
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

    void playingTrackScrollsIntoView()
    {
        App::Core core;
        Ui::WindowHost host(core);
        host.show();
        QSignalSpy ready(&core.sourceManager(), &Rpc::SourceManager::sourceReady);
        core.sourceManager().startAll();
        QVERIFY(ready.wait(10000));

        auto* list = host.window()->findChild<QListView*>(QStringLiteral("trackListView"));
        QVERIFY(list != nullptr);
        QList<Track> tracks;
        for (int i = 0; i < 40; ++i) {
            Track track;
            track.id = QStringLiteral("t%1").arg(i);
            track.title = QStringLiteral("Track %1").arg(i);
            tracks.append(track);
        }
        core.playback().loadQueue(QStringLiteral("fake"), tracks, 30);
        QTRY_COMPARE(core.playback().currentIndex(), 30);
        QTRY_COMPARE(list->model()->rowCount(), 40);
        const auto rowRect = [list](int row) { return list->visualRect(list->model()->index(row, 0)); };
        const QRect viewport = list->viewport()->rect();
        QTRY_VERIFY(viewport.contains(rowRect(30)));
        QCOMPARE(rowRect(30).bottom(), viewport.bottom());

        list->verticalScrollBar()->setValue(0);
        core.playback().playAt(35);
        QTRY_COMPARE(core.playback().currentIndex(), 35);
        QCOMPARE(rowRect(35).bottom(), viewport.bottom());

        const int scroll = list->verticalScrollBar()->value();
        core.playback().playAt(34);
        QTRY_COMPARE(core.playback().currentIndex(), 34);
        QCOMPARE(list->verticalScrollBar()->value(), scroll);

        list->verticalScrollBar()->setValue(list->verticalScrollBar()->maximum());
        core.playback().playAt(3);
        QTRY_COMPARE(core.playback().currentIndex(), 3);
        QCOMPARE(rowRect(3).top(), viewport.top());

        for (Rpc::RpcClient* client : core.sourceManager().clients())
            await(client->shutdown());
    }

    void mprisSeekAndVolumeFollowThePlayer()
    {
        App::Core core;
        Ui::WindowHost host(core);
        host.show();
        QSignalSpy ready(&core.sourceManager(), &Rpc::SourceManager::sourceReady);
        core.sourceManager().startAll();
        QVERIFY(ready.wait(10000));

        QObject adaptorHost;
        Integration::MprisPlayerAdaptor adaptor(core.playback(), core.nowPlaying(), &adaptorHost);
        QSignalSpy seeked(&adaptor, &Integration::MprisPlayerAdaptor::Seeked);
        auto* slider = host.window()->findChild<QSlider*>(QStringLiteral("volumeSlider"));
        QVERIFY(slider != nullptr);
        adaptor.setVolume(0.42);
        QCOMPARE(core.nowPlaying().volume(), 42);
        QCOMPARE(core.settings().volume(), 42);
        QCOMPARE(slider->value(), 42);
        QCOMPARE(adaptor.volume(), 0.42);

        Track track;
        track.id = QStringLiteral("t1");
        track.title = QStringLiteral("Track 1");
        track.durationMs = 180000;
        core.playback().loadQueue(QStringLiteral("fake"), { track }, 0);
        QTRY_VERIFY(core.playback().hasCurrentTrack());
        QVERIFY(adaptor.canSeek());
        const auto trackId = adaptor.metadata().value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>();
        adaptor.SetPosition(QDBusObjectPath(QStringLiteral("/org/cloudmus/Track/stale")), 20000000);
        QCOMPARE(adaptor.position(), 0);
        adaptor.SetPosition(trackId, 20000000);
        QCOMPARE(adaptor.position(), 20000000);
        QCOMPARE(seeked.count(), 1);
        adaptor.Seek(-5000000);
        QCOMPARE(adaptor.position(), 15000000);
        QCOMPARE(seeked.count(), 2);
        adaptor.SetPosition(trackId, 181000000);
        QCOMPARE(adaptor.position(), 15000000);

        if (QDBusConnection::sessionBus().isConnected()) {
            Integration::MprisService service(core.playback(), core.nowPlaying());
            QDBusInterface player(QStringLiteral("org.mpris.MediaPlayer2.cloudmus"),
                QStringLiteral("/org/mpris/MediaPlayer2"), QStringLiteral("org.mpris.MediaPlayer2.Player"));
            QVERIFY(player.isValid());
            QDBusPendingCallWatcher call(player.asyncCall(
                QStringLiteral("SetPosition"), QVariant::fromValue(trackId), QVariant::fromValue(qlonglong(25000000))));
            QSignalSpy finished(&call, &QDBusPendingCallWatcher::finished);
            QVERIFY(finished.wait(5000));
            QVERIFY(!call.isError());
            QCOMPARE(adaptor.position(), 25000000);

            QDBusInterface properties(QStringLiteral("org.mpris.MediaPlayer2.cloudmus"),
                QStringLiteral("/org/mpris/MediaPlayer2"), QStringLiteral("org.freedesktop.DBus.Properties"));
            QDBusPendingCallWatcher setVolume(
                properties.asyncCall(QStringLiteral("Set"), QStringLiteral("org.mpris.MediaPlayer2.Player"),
                    QStringLiteral("Volume"), QVariant::fromValue(QDBusVariant(0.36))));
            QSignalSpy volumeSet(&setVolume, &QDBusPendingCallWatcher::finished);
            QVERIFY(volumeSet.wait(5000));
            QVERIFY(!setVolume.isError());
            QCOMPARE(core.nowPlaying().volume(), 36);
            QCOMPARE(core.settings().volume(), 36);
            QCOMPARE(slider->value(), 36);
        }

        core.playback().stop();
        QVERIFY(!adaptor.canSeek());
        QCOMPARE(adaptor.position(), 0);
        QVERIFY(adaptor.metadata().isEmpty());
        for (Rpc::RpcClient* client : core.sourceManager().clients())
            await(client->shutdown());
    }
};

QObject* makeWindowHostTest() { return new WindowHostTest; }

} // namespace Tests

#include "WindowHostTest.moc"
