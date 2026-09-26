#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "ActivePlaylist.h"
#include "AuthStates.h"
#include "CoverArtCache.h"
#include "FakeBackend.h"
#include "Messages.h"
#include "PlaybackController.h"
#include "PlaybackHistory.h"
#include "PlaylistEditing.h"
#include "RpcClient.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "Sources.h"
#include "TestSupport.h"
#include "TrackStates.h"

namespace Tests {

class ActivePlaylistTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { installFakeBackendManifest(); }

    void init()
    {
        // Fresh settings: the saved active playlist lives there.
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/cloudmus/fronts/qt/config.ini"));
        settings_ = std::make_unique<Config::Settings>();
    }

    void cleanup()
    {
        for (Rpc::RpcClient* client : sourceManager_->clients())
            await(client->shutdown());
        active_.reset();
        sources_.reset();
        editing_.reset();
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
        settings_.reset();
    }

    void playingAPlaylistMakesItActiveAndRemembersIt()
    {
        start();
        QSignalSpy activated(active_.get(), &ViewModel::ActivePlaylist::activated);
        await(active_->activateAndPlay(QStringLiteral("fake"), playlist(QStringLiteral("p1"))));
        QCOMPARE(activated.count(), 1);
        QCOMPARE(active_->context().playlist.id, QStringLiteral("p1"));
        QCOMPARE(active_->entries().size(), 2);
        QCOMPARE(settings_->lastActivePlaylist().playlistId, QStringLiteral("p1"));
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t1"));
    }

    void theLastActivePlaylistComesBackOnceItsSourceListsIt()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        start();
        QTRY_COMPARE(active_->context().playlist.id, QStringLiteral("p1"));
        QTRY_COMPARE(active_->entries().size(), 2);
        QVERIFY(!playback_->hasQueue()); // back, but not playing
    }

    void anEditToTheActivePlaylistShowsInItsTracks()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        start();
        QTRY_COMPARE(active_->entries().size(), 2);
        Track added;
        added.id = QStringLiteral("t3");
        QVERIFY(
            await(editing_->setTrackInPlaylist(QStringLiteral("fake"), added, playlist(QStringLiteral("p1")), true)));
        QCOMPARE(active_->entries().size(), 3);
        QCOMPARE(active_->context().playlist.trackCount, 3);
    }

private:
    static Playlist playlist(const QString& id)
    {
        Playlist p;
        p.id = id;
        p.title = id;
        p.kind = QStringLiteral("playlist");
        return p;
    }

    // Everything ActivePlaylist stands on, then the source started and
    // listed.
    void start()
    {
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        session_ = std::make_unique<App::SourceSession>(
            *sourceManager_, *playback_, authStates_, trackStates_, coverArtCache_, messages_);
        editing_ = std::make_unique<App::PlaylistEditing>(*sourceManager_, messages_);
        sources_ = std::make_unique<ViewModel::Sources>(
            *sourceManager_, *session_, authStates_, *editing_, coverArtCache_, *settings_, messages_);
        active_ = std::make_unique<ViewModel::ActivePlaylist>(*playback_, *sourceManager_, trackStates_, coverArtCache_,
            history_, *settings_, *sources_, *editing_, messages_);
        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(loaded.wait(10000));
    }

    Rpc::AuthStates authStates_;
    Library::TrackStates trackStates_;
    Covers::CoverArtCache coverArtCache_;
    History::PlaybackHistory history_;
    ViewModel::Messages messages_;
    std::unique_ptr<Config::Settings> settings_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<Playback::PlaybackController> playback_;
    std::unique_ptr<App::SourceSession> session_;
    std::unique_ptr<App::PlaylistEditing> editing_;
    std::unique_ptr<ViewModel::Sources> sources_;
    std::unique_ptr<ViewModel::ActivePlaylist> active_;
};

QObject* makeActivePlaylistTest() { return new ActivePlaylistTest; }

} // namespace Tests

#include "ActivePlaylistTest.moc"
