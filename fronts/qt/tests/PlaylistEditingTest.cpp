#include <QSignalSpy>
#include <QTest>

#include "FakeBackend.h"
#include "Messages.h"
#include "PlaylistEditing.h"
#include "RpcClient.h"
#include "RpcMethods.h"
#include "SourceManager.h"
#include "TestSupport.h"

namespace Tests {

class PlaylistEditingTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { installFakeBackendManifest(); }

    void init()
    {
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        editing_ = std::make_unique<App::PlaylistEditing>(*sourceManager_, messages_);
        QSignalSpy ready(sourceManager_.get(), &Rpc::SourceManager::sourceReady);
        sourceManager_->startAll();
        QVERIFY(ready.wait(10000));
        const ListPlaylistsResult listed = await(Rpc::catalogListPlaylists(*client()));
        editing_->setPlaylists(QStringLiteral("fake"), listed.playlists);
    }

    void cleanup()
    {
        for (Rpc::RpcClient* c : sourceManager_->clients())
            await(c->shutdown());
        editing_.reset();
        sourceManager_.reset();
    }

    void onlyTheUsersOwnPlaylistsAreEditable()
    {
        QVERIFY(editing_->canEdit(QStringLiteral("fake")));
        const QList<Playlist> editable = editing_->editablePlaylists(QStringLiteral("fake"));
        QCOMPARE(editable.size(), 1);
        QCOMPARE(editable.first().id, QStringLiteral("p1"));
    }

    void membershipSaysWhichPlaylistsHoldTheTrack()
    {
        const auto membership = await(editing_->membership(QStringLiteral("fake"), QStringLiteral("t1")));
        QVERIFY(membership.has_value());
        QCOMPARE(membership->playlists.size(), 1);
        QCOMPARE(membership->containing, QStringList { QStringLiteral("p1") });
    }

    void anAddIsAnnouncedWithTheNewCount()
    {
        QSignalSpy edited(editing_.get(), &App::PlaylistEditing::playlistEdited);
        QSignalSpy posted(&messages_, &ViewModel::Messages::posted);
        const Playlist playlist = editing_->editablePlaylists(QStringLiteral("fake")).first();
        QVERIFY(
            await(editing_->setTrackInPlaylist(QStringLiteral("fake"), track(QStringLiteral("t1")), playlist, true)));
        QCOMPARE(edited.count(), 1);
        QCOMPARE(edited.first().at(2).toString(), QStringLiteral("p1"));
        QCOMPARE(edited.first().at(3).toBool(), true);
        QCOMPARE(edited.first().at(4).toInt(), 3);
        QCOMPARE(editing_->editablePlaylists(QStringLiteral("fake")).first().trackCount, 3);
        QCOMPARE(posted.first().at(1).toString(), QStringLiteral("Added to \"First\""));
    }

    void aFailedRemovalChangesNothingAndSaysWhy()
    {
        QSignalSpy edited(editing_.get(), &App::PlaylistEditing::playlistEdited);
        QSignalSpy posted(&messages_, &ViewModel::Messages::posted);
        const Playlist playlist = editing_->editablePlaylists(QStringLiteral("fake")).first();
        QVERIFY(!await(
            editing_->setTrackInPlaylist(QStringLiteral("fake"), track(QStringLiteral("fail")), playlist, false)));
        QCOMPARE(edited.count(), 0);
        QCOMPARE(posted.first().at(1).toString(), QStringLiteral("Fake Source: not in the playlist"));
    }

private:
    static Track track(const QString& id)
    {
        Track t;
        t.id = id;
        return t;
    }
    Rpc::RpcClient* client() const { return sourceManager_->client(QStringLiteral("fake")); }

    ViewModel::Messages messages_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<App::PlaylistEditing> editing_;
};

QObject* makePlaylistEditingTest() { return new PlaylistEditingTest; }

} // namespace Tests

#include "PlaylistEditingTest.moc"
