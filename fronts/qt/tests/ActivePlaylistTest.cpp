#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
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
        temp_ = std::make_unique<QTemporaryDir>();
        settings_ = std::make_unique<Config::Settings>(temp_->filePath(QStringLiteral("config.ini")));
    }

    void cleanup()
    {
        resetApp();
        temp_.reset();
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

    void theLastSongAndTrackListComeBackWithoutAutoplay()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        start();
        QTRY_COMPARE(active_->entries().size(), 2);
        active_->playRow(1);
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
        QCOMPARE(settings_->lastActiveTrackId(), QStringLiteral("t2"));

        resetApp();
        settings_ = std::make_unique<Config::Settings>(temp_->filePath(QStringLiteral("config.ini")));
        start(false);
        QCOMPARE(active_->context().playlist.id, QStringLiteral("p1"));
        QCOMPARE(active_->entries().size(), 2); // cached before the source starts
        QCOMPARE(active_->resumeIndex(), 1);
        QVERIFY(!playback_->hasQueue());

        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(loaded.wait(10000));
        active_->play();
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
    }

    void aDynamicPlaylistRestoresItsTracksAndResumesItsLastSong()
    {
        start();
        Playlist station = playlist(QStringLiteral("wave"));
        station.kind = QStringLiteral("radioStation");
        active_->setContext({ QStringLiteral("fake"), station });
        Track first;
        first.id = QStringLiteral("old-1");
        first.title = QStringLiteral("Old track 1");
        Track second;
        second.id = QStringLiteral("old-2");
        second.title = QStringLiteral("Old track 2");
        second.liked = false;
        playback_->startRadio(QStringLiteral("fake"), QStringLiteral("old-station"), { first, second });
        QTRY_VERIFY(playback_->hasCurrentTrack());
        playback_->playAt(1);
        QTRY_COMPARE(settings_->lastActiveTrackId(), QStringLiteral("old-2"));

        resetApp();
        settings_ = std::make_unique<Config::Settings>(temp_->filePath(QStringLiteral("config.ini")));
        start(false);
        QCOMPARE(active_->context().playlist.id, QStringLiteral("wave"));
        QCOMPARE(active_->entries().size(), 2);
        QCOMPARE(active_->entries()[0].track.id, QStringLiteral("old-1"));
        QCOMPARE(active_->entries()[1].track.id, QStringLiteral("old-2"));
        QCOMPARE(trackStates_.state(QStringLiteral("fake"), QStringLiteral("old-2")).liked, std::optional<bool>(false));
        QVERIFY(!playback_->hasQueue());

        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(loaded.wait(10000));
        QTRY_COMPARE(
            trackStates_.state(QStringLiteral("fake"), QStringLiteral("old-2")).liked, std::optional<bool>(true));
        QFile cache(temp_->filePath(QStringLiteral("active-playlist.json")));
        QVERIFY(cache.open(QIODevice::ReadOnly));
        const QJsonArray cachedRows
            = QJsonDocument::fromJson(cache.readAll()).object().value(QStringLiteral("tracks")).toArray();
        QCOMPARE(cachedRows[1].toObject().value(QStringLiteral("track")).toObject().value(QStringLiteral("liked")),
            QJsonValue(true));
        active_->play();
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("old-2"));
        QCOMPARE(trackStates_.state(QStringLiteral("fake"), QStringLiteral("old-2")).liked, std::optional<bool>(true));
        QVERIFY(playback_->isRadio());
        QCOMPARE(active_->entries().size(), 2); // resumed track, then the new station's first track
        QCOMPARE(active_->entries()[1].track.id, QStringLiteral("fresh"));
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
    void resetApp()
    {
        if (sourceManager_) {
            for (Rpc::RpcClient* client : sourceManager_->clients())
                await(client->shutdown());
        }
        active_.reset();
        sources_.reset();
        editing_.reset();
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
        settings_.reset();
    }

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
    void start(bool startSources = true)
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
        if (startSources) {
            QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
            sourceManager_->startAll();
            QVERIFY(loaded.wait(10000));
        }
    }

    Rpc::AuthStates authStates_;
    Library::TrackStates trackStates_;
    Covers::CoverArtCache coverArtCache_;
    History::PlaybackHistory history_;
    ViewModel::Messages messages_;
    std::unique_ptr<Config::Settings> settings_;
    std::unique_ptr<QTemporaryDir> temp_;
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
