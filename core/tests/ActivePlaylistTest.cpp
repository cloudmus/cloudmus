#include <algorithm>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include "ActivePlaylist.h"
#include "AuthStates.h"
#include "CoverArtCache.h"
#include "FakeBackend.h"
#include "FakeUpstream.h"
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
#include "TrackFetch.h"
#include "TrackCache.h"
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

    // A radio started from a track is no playlist of its source, yet it is
    // what comes back — not the playlist that was active before it.
    void aRadioFromATrackIsRestoredAndResumed()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        start();
        Playlist station = playlist(QStringLiteral("seed-track"));
        station.kind = QStringLiteral("radioStation");
        station.title = QStringLiteral("Radio: Seed");
        active_->setContext({ QStringLiteral("fake"), station });
        Track first;
        first.id = QStringLiteral("old-1");
        Track second;
        second.id = QStringLiteral("old-2");
        playback_->startRadio(QStringLiteral("fake"), QStringLiteral("old-station"), { first, second });
        QTRY_VERIFY(playback_->hasCurrentTrack());
        playback_->playAt(1);
        QTRY_COMPARE(settings_->lastActiveTrackId(), QStringLiteral("old-2"));

        resetApp();
        settings_ = std::make_unique<Config::Settings>(temp_->filePath(QStringLiteral("config.ini")));
        settings_->setResumePlaybackAtStartup(true);
        start(false);
        QCOMPARE(active_->context().playlist.id, QStringLiteral("seed-track"));
        QCOMPARE(active_->context().playlist.title, QStringLiteral("Radio: Seed"));
        QCOMPARE(active_->entries().size(), 2);

        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(loaded.wait(10000));
        QCOMPARE(active_->context().playlist.id, QStringLiteral("seed-track"));
        active_->play();
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("old-2"));
        QVERIFY(playback_->isRadio());
    }

    // A tap on a saved radio's song while its source is still starting plays
    // that song of what's saved, not a new station's tracks later.
    void aTapOnASavedRadioBeforeItsSourceIsUpPlaysThatSong()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        start();
        Playlist station = playlist(QStringLiteral("seed-track"));
        station.kind = QStringLiteral("radioStation");
        active_->setContext({ QStringLiteral("fake"), station });
        Track first;
        first.id = QStringLiteral("old-1");
        Track second;
        second.id = QStringLiteral("old-2");
        playback_->startRadio(QStringLiteral("fake"), QStringLiteral("old-station"), { first, second });
        QTRY_VERIFY(playback_->hasCurrentTrack());

        resetApp();
        settings_ = std::make_unique<Config::Settings>(temp_->filePath(QStringLiteral("config.ini")));
        start(false);
        QCOMPARE(active_->entries().size(), 2);
        QSignalSpy errors(playback_.get(), &Playback::PlaybackController::errorOccurred);
        active_->playRow(1);
        QVERIFY(playback_->hasQueue());
        sourceManager_->startAll();
        QTRY_VERIFY_WITH_TIMEOUT(playback_->hasCurrentTrack(), 10000);
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("old-2"));
        QVERIFY(playback_->isRadio());
        QCOMPARE(errors.count(), 0);
    }

    void playbackGoingAtQuitResumesFromTheSameSong()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        settings_->setLastActiveTrack(QStringLiteral("t2"), 1);
        settings_->setWasPlaying(true);
        settings_->setResumePlaybackAtStartup(true);
        start();
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
    }

    // A car starting, "play" on the steering wheel: asked for by hand, whatever
    // the setting says, and before the source has even listed its playlists.
    void resumeAskedForGoesOnFromTheSavedSong()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        settings_->setLastActiveTrack(QStringLiteral("t2"), 1);
        start();
        QVERIFY(!playback_->hasQueue());
        active_->resume();
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
    }

    // No network, and a song that isn't on disk: on to the next one that is,
    // quietly. The playlist and its tracks come from what was saved.
    void withNoNetworkItPlaysWhatIsOnDisk()
    {
        const QString down = temp_->filePath(QStringLiteral("network-down"));
        qputenv("CLOUDMUS_FAKE_OFFLINE_FILE", down.toUtf8());
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        start();
        QTRY_COMPARE(active_->entries().size(), 2);
        active_->playRow(0);
        QTRY_VERIFY(playback_->hasCurrentTrack());

        resetApp();
        settings_ = std::make_unique<Config::Settings>(temp_->filePath(QStringLiteral("config.ini")));
        QFile(down).open(QIODevice::WriteOnly);
        seedCached(QStringLiteral("t2"));
        messages_.setOfflineShownElsewhere(true);
        start();
        QCOMPARE(active_->entries().size(), 2);
        QSignalSpy errors(playback_.get(), &Playback::PlaybackController::errorOccurred);
        QSignalSpy notices(playback_.get(), &Playback::PlaybackController::offlineNotice);
        active_->resume();
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
        QCOMPARE(errors.size(), 0);
        QVERIFY(notices.size() >= 1);

        // Back: the tracks that couldn't load are asked for again.
        QFile::remove(down);
        QSignalSpy loading(active_.get(), &ViewModel::ActivePlaylist::loadingChanged);
        sources_->refresh(QStringLiteral("fake"));
        QTRY_VERIFY(loading.size() >= 2);
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2")); // not started over
        qunsetenv("CLOUDMUS_FAKE_OFFLINE_FILE");
        messages_.setOfflineShownElsewhere(false);
    }

    // A playlist played once plays again with no network, from the tracks it
    // had: nothing to wait for, nothing to fail.
    void aPlaylistPlaysFromItsSavedTracksWhenNothingAnswers()
    {
        const QString down = temp_->filePath(QStringLiteral("network-down"));
        qputenv("CLOUDMUS_FAKE_OFFLINE_FILE", down.toUtf8());
        start();
        await(active_->activateAndPlay(QStringLiteral("fake"), playlist(QStringLiteral("p1"))));
        QCOMPARE(active_->entries().size(), 2);

        resetApp();
        settings_ = std::make_unique<Config::Settings>(temp_->filePath(QStringLiteral("config.ini")));
        QFile(down).open(QIODevice::WriteOnly);
        messages_.setOfflineShownElsewhere(true);
        // The source isn't even started: there is nobody to ask.
        start(false);
        QSignalSpy activated(active_.get(), &ViewModel::ActivePlaylist::activated);
        QSignalSpy errors(&messages_, &ViewModel::Messages::posted);
        await(active_->activateAndPlay(QStringLiteral("fake"), playlist(QStringLiteral("p1"))));
        QCOMPARE(activated.count(), 1);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(active_->entries().size(), 2);
        QCOMPARE(active_->context().playlist.id, QStringLiteral("p1"));
        qunsetenv("CLOUDMUS_FAKE_OFFLINE_FILE");
        messages_.setOfflineShownElsewhere(false);
    }

    // A tap right after launch, with the source still starting, isn't lost:
    // the track plays the moment the source is up.
    void aTrackPickedBeforeItsSourceIsUpPlaysOnceItIs()
    {
        start(false);
        QSignalSpy errors(playback_.get(), &Playback::PlaybackController::errorOccurred);
        Track first;
        first.id = QStringLiteral("t1");
        Track second;
        second.id = QStringLiteral("t2");
        playback_->loadQueue(QStringLiteral("fake"), { first, second }, 1);
        QTest::qWait(100);
        QCOMPARE(errors.count(), 0);
        QVERIFY(!playback_->hasCurrentTrack());
        sourceManager_->startAll();
        QTRY_VERIFY_WITH_TIMEOUT(playback_->hasCurrentTrack(), 10000);
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
        QCOMPARE(errors.count(), 0);
    }

    void thePlaylistTrackCacheKeepsWhatItWasGivenAndForgetsASource()
    {
        App::PlaylistTrackCache cache(temp_->filePath(QStringLiteral("pt")));
        QVERIFY(!cache.load(QStringLiteral("fake"), QStringLiteral("p1")));
        Track track;
        track.id = QStringLiteral("t9");
        track.title = QStringLiteral("Nine");
        cache.save(QStringLiteral("fake"), QStringLiteral("p1"), { { QStringLiteral("fake"), track } });
        const auto loaded = cache.load(QStringLiteral("fake"), QStringLiteral("p1"));
        QVERIFY(loaded);
        QCOMPARE(loaded->size(), 1);
        QCOMPARE(loaded->first().track.id, QStringLiteral("t9"));
        QVERIFY(!cache.load(QStringLiteral("fake"), QStringLiteral("p2")));
        cache.removeSource(QStringLiteral("fake"));
        QVERIFY(!cache.load(QStringLiteral("fake"), QStringLiteral("p1")));
    }

    // With a delay asked for, what's saved stands for now and is freshened after.
    void savedTracksCanBeFreshenedLater()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        start();
        QTRY_COMPARE(active_->entries().size(), 2);
        active_->playRow(0);
        QTRY_VERIFY(playback_->hasCurrentTrack());
        resetApp();
        settings_ = std::make_unique<Config::Settings>(temp_->filePath(QStringLiteral("config.ini")));
        start(false);
        active_->setRefreshDelay(400);
        QSignalSpy loading(active_.get(), &ViewModel::ActivePlaylist::loadingChanged);
        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(loaded.wait(10000));
        QCOMPARE(active_->entries().size(), 2);
        QCOMPARE(loading.size(), 0);
        QTRY_VERIFY(loading.size() >= 1);
    }

    // Back to the song, and to where in it.
    void resumingGoesOnFromWhereInTheSongItWas()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        settings_->setLastActiveTrack(QStringLiteral("t2"), 1);
        settings_->setLastActivePositionMs(12000);
        seedCached(QStringLiteral("t2"));
        start();
        active_->resume();
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
        QTRY_VERIFY(playback_->positionMs() >= 12000);
        // Another song starts from its beginning.
        settings_->setLastActiveTrack(QStringLiteral("t1"), 0);
        QCOMPARE(settings_->lastActivePositionMs(), qint64(0));
    }

    void resumeWithNothingSavedDoesNothing()
    {
        start();
        active_->resume();
        QTest::qWait(100);
        QVERIFY(!playback_->hasQueue());
    }

    void noResumeWhenItsOffOrPlaybackWasHalted()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        settings_->setWasPlaying(true);
        start();
        QTRY_COMPARE(active_->entries().size(), 2);
        QVERIFY(!playback_->hasQueue());

        // Stopping is what the next run goes by.
        active_->play();
        QTRY_VERIFY(playback_->hasCurrentTrack());
        playback_->stop();
        QVERIFY(!settings_->wasPlaying());

        resetApp();
        settings_ = std::make_unique<Config::Settings>(temp_->filePath(QStringLiteral("config.ini")));
        settings_->setResumePlaybackAtStartup(true);
        start();
        QTRY_COMPARE(active_->entries().size(), 2);
        QVERIFY(!playback_->hasQueue());
    }

    // The song is on disk, so nothing needs its source: it plays before the
    // source has even started, and goes on playing when it is up.
    void aDownloadedSongResumesBeforeItsSourceStarts()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        start();
        QTRY_COMPARE(active_->entries().size(), 2);
        active_->playRow(1);
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(settings_->lastActiveTrackId(), QStringLiteral("t2"));

        restartApp();
        seedCached(QStringLiteral("t2"));
        start(false);
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
        QVERIFY(!playback_->isRadio());

        QSignalSpy changed(playback_.get(), &Playback::PlaybackController::trackChanged);
        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(loaded.wait(10000));
        QTRY_COMPARE(active_->entries().size(), 2);
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
        QCOMPARE(changed.size(), 0); // not started over
    }

    void aSongThatIsNotDownloadedWaitsForItsSource()
    {
        settings_->setLastActivePlaylist({ QStringLiteral("fake"), QStringLiteral("p1"), QStringLiteral("playlist") });
        start();
        QTRY_COMPARE(active_->entries().size(), 2);
        active_->playRow(1);
        QTRY_VERIFY(playback_->hasCurrentTrack());

        restartApp();
        start(false);
        QTest::qWait(300);
        QVERIFY(!playback_->hasQueue());

        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(loaded.wait(10000));
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("t2"));
    }

    // The old station's tracks play from disk; once its source is up a
    // station is started behind them and keeps the queue filling.
    void aDownloadedRadioSongResumesAndGetsItsStationLater()
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
        playback_->startRadio(QStringLiteral("fake"), QStringLiteral("old-station"), { first, second });
        QTRY_VERIFY(playback_->hasCurrentTrack());
        playback_->playAt(1);
        QTRY_COMPARE(settings_->lastActiveTrackId(), QStringLiteral("old-2"));

        restartApp();
        seedCached(QStringLiteral("old-2"));
        start(false);
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("old-2"));
        QVERIFY(playback_->isRadio());
        QVERIFY(playback_->awaitsStation());

        QSignalSpy changed(playback_.get(), &Playback::PlaybackController::trackChanged);
        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sourceManager_->startAll();
        QVERIFY(loaded.wait(10000));
        QTRY_VERIFY(!playback_->awaitsStation());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("old-2"));
        QCOMPARE(changed.size(), 0);
        QTRY_VERIFY(playback_->queue().size() > 2);
        QCOMPARE(playback_->queue().last().track.id, QStringLiteral("fresh"));
    }

    // A radio with no station to be had goes on past its last downloaded
    // track with others on the disk, from History: not disliked ones, and
    // not one already queued.
    void aRadioWithNoStationGoesOnWithDownloadedTracks()
    {
        for (const char* id : { "pool-1", "pool-2", "pool-3", "pool-disliked", "pool-start" })
            seedCached(QString::fromLatin1(id));
        start(false);
        wireOfflineRadio();
        for (const char* id : { "pool-1", "pool-2", "pool-3", "pool-disliked", "not-downloaded" })
            history_.record(QStringLiteral("fake"), track(QString::fromLatin1(id)));
        trackStates_.setDisliked(QStringLiteral("fake"), QStringLiteral("pool-disliked"), true);
        Playlist station = playlist(QStringLiteral("wave"));
        station.kind = QStringLiteral("radioStation");
        active_->setContext({ QStringLiteral("fake"), station });
        playback_->startRadio(QStringLiteral("fake"), QString(), { track(QStringLiteral("pool-start")) });
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QVERIFY(playback_->awaitsStation());

        const QStringList pool { QStringLiteral("pool-1"), QStringLiteral("pool-2"), QStringLiteral("pool-3") };
        QStringList played;
        for (int i = 0; i < 3; ++i) {
            playback_->next();
            QTRY_VERIFY(playback_->hasCurrentTrack() && playback_->currentTrack().id != QStringLiteral("pool-start")
                && !played.contains(playback_->currentTrack().id));
            played.append(playback_->currentTrack().id);
        }
        std::sort(played.begin(), played.end());
        QCOMPARE(played, pool);
        for (const Playback::QueueEntry& entry : playback_->queue()) {
            QVERIFY(entry.track.id != QStringLiteral("pool-disliked"));
            QVERIFY(entry.track.id != QStringLiteral("not-downloaded"));
        }
        trackStates_.setDisliked(QStringLiteral("fake"), QStringLiteral("pool-disliked"), false);
    }

    // A running station whose service is gone: its queue's end goes on
    // with what's on the disk at once, not after waiting for the station.
    void aStationGoneOfflineGoesOnFromTheDiskAtOnce()
    {
        seedCached(QStringLiteral("gone-1"));
        seedCached(QStringLiteral("gone-start"));
        start(false);
        wireOfflineRadio();
        playback_->setFeedbackHold([](const QString&) { return true; }); // the service is offline
        history_.record(QStringLiteral("fake"), track(QStringLiteral("gone-1")));
        playback_->startRadio(
            QStringLiteral("fake"), QStringLiteral("station"), { track(QStringLiteral("gone-start")) });
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QSignalSpy errors(playback_.get(), &Playback::PlaybackController::failed);
        playback_->next();
        QTRY_COMPARE_WITH_TIMEOUT(playback_->currentTrack().id, QStringLiteral("gone-1"), 3000);
        QVERIFY(!playback_->awaitsStation());
        QCOMPARE(errors.size(), 0);
    }

    // Started with no network and nothing saved of it: what's on the disk
    // plays, and the station attaches once the network is back.
    void aRadioStartedWithNoNetworkPlaysDownloadedTracks()
    {
        const QString down = temp_->filePath(QStringLiteral("network-down"));
        qputenv("CLOUDMUS_FAKE_OFFLINE_FILE", down.toUtf8());
        QFile(down).open(QIODevice::WriteOnly);
        seedCached(QStringLiteral("offline-1"));
        start(false);
        wireOfflineRadio();
        history_.record(QStringLiteral("fake"), track(QStringLiteral("offline-1")));
        QSignalSpy ready(sourceManager_.get(), &Rpc::SourceManager::sourceReady);
        sourceManager_->startAll();
        QVERIFY(ready.wait(10000));
        Playlist station = playlist(QStringLiteral("wave"));
        station.kind = QStringLiteral("radioStation");
        await(active_->startRadio(QStringLiteral("fake"), station.id, { QStringLiteral("fake"), station }));
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("offline-1"));
        QVERIFY(playback_->isRadio());
        QVERIFY(playback_->awaitsStation());
        QCOMPARE(active_->context().playlist.id, QStringLiteral("wave"));

        QFile::remove(down);
        QSignalSpy loaded(sources_.get(), &ViewModel::Sources::playlistsLoaded);
        sources_->refresh(QStringLiteral("fake"));
        QVERIFY(loaded.wait(10000));
        QTRY_VERIFY(!playback_->awaitsStation());
        QCOMPARE(playback_->currentTrack().id, QStringLiteral("offline-1"));
        qunsetenv("CLOUDMUS_FAKE_OFFLINE_FILE");
    }

    // Another radio playing, and a station started with no network: what's
    // downloaded of the new one's source plays in its place.
    void aStationStartedOfflineOverAnotherRadioPlaysDownloadedTracks()
    {
        const QString down = temp_->filePath(QStringLiteral("network-down"));
        qputenv("CLOUDMUS_FAKE_OFFLINE_FILE", down.toUtf8());
        seedCached(QStringLiteral("over-1"));
        seedCached(QStringLiteral("other-radio"));
        start();
        wireOfflineRadio();
        history_.record(QStringLiteral("fake"), track(QStringLiteral("over-1")));
        Playlist other = playlist(QStringLiteral("other-wave"));
        other.kind = QStringLiteral("radioStation");
        active_->setContext({ QStringLiteral("fake"), other });
        playback_->startRadio(
            QStringLiteral("fake"), QStringLiteral("station"), { track(QStringLiteral("other-radio")) });
        QTRY_VERIFY(playback_->hasCurrentTrack());

        QFile(down).open(QIODevice::WriteOnly);
        Playlist station = playlist(QStringLiteral("wave"));
        station.kind = QStringLiteral("radioStation");
        active_->activate({ QStringLiteral("fake"), station }, { }, 0);
        QTRY_COMPARE(active_->context().playlist.id, QStringLiteral("wave"));
        QTRY_COMPARE(playback_->currentTrack().id, QStringLiteral("over-1"));
        qunsetenv("CLOUDMUS_FAKE_OFFLINE_FILE");
    }

    // A service known to be offline isn't asked for a station (with no
    // network the asking can time out instead of failing): the disk plays.
    // With nothing downloaded of it, that is said — not left silent.
    void aStationOfAnOfflineServiceStartsFromTheDiskOrSaysWhyNot()
    {
        // History outlives the test run: a name of this run's own.
        const QString id = QStringLiteral("known-offline-") + QUuid::createUuid().toString(QUuid::Id128);
        seedCached(id);
        start();
        wireOfflineRadio();
        bool offline = true;
        active_->setOfflineProvider([&offline](const QString&) { return offline; });
        Playlist station = playlist(QStringLiteral("wave"));
        station.kind = QStringLiteral("radioStation");
        QSignalSpy posted(&messages_, &ViewModel::Messages::posted);

        // Nothing of it on the disk that History knows of yet (this cache
        // directory is the test's own).
        QVERIFY(active_->cachedRadioTracks(QStringLiteral("fake"), 5).isEmpty());
        active_->activate({ QStringLiteral("fake"), station }, { }, 0);
        QTRY_COMPARE(posted.size(), 1);
        QCOMPARE(posted.first().first().value<ViewModel::Messages::Kind>(), ViewModel::Messages::Kind::Error);
        QVERIFY(!playback_->hasQueue());

        history_.record(QStringLiteral("fake"), track(id));
        active_->activate({ QStringLiteral("fake"), station }, { }, 0);
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QCOMPARE(playback_->currentTrack().id, id);
        QVERIFY(playback_->awaitsStation()); // the source wasn't asked
        offline = false;
    }

    // A service says "unavailable" of tracks that play later: picking one
    // yourself tries it again, and the mark fades by itself.
    void anUnavailableTrackIsTriedAgainWhenPicked()
    {
        start();
        playback_->loadQueue(QStringLiteral("fake"), { track(QStringLiteral("a")), track(QStringLiteral("b")) }, 0);
        QTRY_VERIFY(playback_->hasCurrentTrack());
        playback_->setUnavailable(QStringLiteral("fake"), QStringLiteral("b"));
        QVERIFY(playback_->isUnavailable(QStringLiteral("fake"), QStringLiteral("b")));
        QSignalSpy again(playback_.get(), &Playback::PlaybackController::trackAvailableAgain);
        playback_->playAt(1);
        QVERIFY(!playback_->isUnavailable(QStringLiteral("fake"), QStringLiteral("b")));
        QCOMPARE(again.size(), 1);
    }

    // A station is told what played lately from its source — as far back
    // as that source's setting says — to keep it from playing that again.
    void aRadioIsStartedWithTheTracksPlayedLately()
    {
        start();
        QCOMPARE(settings_->sourceRadioRepeatGap(QStringLiteral("fake")), Config::Settings::kDefaultRadioRepeatGap);
        const auto heard = [this](const QString& sourceId, const QString& id) {
            Track track;
            track.id = id;
            track.title = QStringLiteral("Title of ") + id;
            history_.record(sourceId, track);
        };
        heard(QStringLiteral("fake"), QStringLiteral("heard-1"));
        heard(QStringLiteral("fake"), QStringLiteral("heard-2"));
        heard(QStringLiteral("fake"), QStringLiteral("heard-3"));
        heard(QStringLiteral("another"), QStringLiteral("heard-elsewhere"));
        heard(QStringLiteral("fake"), QStringLiteral("heard-2")); // once more: counted once, as the latest
        QCOMPARE(history_.recentTrackIds(QStringLiteral("fake"), 2),
            (QStringList { QStringLiteral("heard-2"), QStringLiteral("heard-3") }));

        settings_->setSourceRadioRepeatGap(QStringLiteral("fake"), 2);
        Playlist station = playlist(QStringLiteral("recent-check"));
        station.kind = QStringLiteral("radioStation");
        active_->activate({ QStringLiteral("fake"), station }, { }, 0);
        // The fake station plays back what it was told.
        QTRY_COMPARE(playback_->queue().size(), 2);
        QCOMPARE(playback_->queue()[0].track.id, QStringLiteral("recent:heard-2"));
        QCOMPARE(playback_->queue()[1].track.id, QStringLiteral("recent:heard-3"));
        // ...and the titles, for a source to tell other versions of these.
        QCOMPARE(playback_->queue()[0].track.title, QStringLiteral("recent:Title of heard-2"));

        settings_->setSourceRadioRepeatGap(QStringLiteral("fake"), 9999);
        QCOMPARE(settings_->sourceRadioRepeatGap(QStringLiteral("fake")), Config::Settings::kMaxRadioRepeatGap);

        // Made anew, a radio's queue is a new station's, with the settings
        // as they are now.
        settings_->setSourceRadioRepeatGap(QStringLiteral("fake"), 1);
        active_->restartRadio();
        QTRY_COMPARE(playback_->queue().size(), 1);
        QCOMPARE(playback_->queue()[0].track.id, QStringLiteral("recent:heard-2"));
    }

    // A repeat gap in days reaches past the count of tracks, never short
    // of it.
    void theRecentTracksReachBackSoManyDaysIfThatIsMore()
    {
        const QString sourceId = QStringLiteral("days-%1").arg(QDateTime::currentMSecsSinceEpoch());
        for (const QString& id : { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") }) {
            Track track;
            track.id = id;
            history_.record(sourceId, track);
        }
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QCOMPARE(history_.recentTrackIds(sourceId, 1, now.addDays(-1)),
            (QStringList { QStringLiteral("c"), QStringLiteral("b"), QStringLiteral("a") }));
        QCOMPARE(history_.recentTrackIds(sourceId, 2, now.addDays(1)),
            (QStringList { QStringLiteral("c"), QStringLiteral("b") }));

        settings_->setSourceRadioRepeatDays(sourceId, 9999);
        QCOMPARE(settings_->sourceRadioRepeatDays(sourceId), Config::Settings::kMaxRadioRepeatDays);
    }

    // Listening to one source doesn't push another's tracks out of History,
    // and so out of what its radio keeps from repeating.
    void historyIsCappedPerSource()
    {
        Track kept;
        kept.id = QStringLiteral("kept");
        history_.record(QStringLiteral("quiet"), kept);
        for (int i = 0; i <= Config::Settings::kMaxRadioRepeatGap; ++i) {
            Track track;
            track.id = QStringLiteral("busy-%1").arg(i);
            history_.record(QStringLiteral("busy"), track);
        }
        QCOMPARE(history_.recentTrackIds(QStringLiteral("quiet"), 1), QStringList { QStringLiteral("kept") });
        QCOMPARE(history_.recentTrackIds(QStringLiteral("busy"), Config::Settings::kMaxRadioRepeatGap + 1).size(),
            Config::Settings::kMaxRadioRepeatGap);
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
        playlistTracks_.reset();
        sources_.reset();
        editing_.reset();
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
        settings_.reset();
    }

    // Starts over from what is on disk: settings and the track cache.
    void restartApp()
    {
        resetApp();
        settings_ = std::make_unique<Config::Settings>(temp_->filePath(QStringLiteral("config.ini")));
        settings_->setResumePlaybackAtStartup(true);
        settings_->setWasPlaying(true);
    }

    void seedCached(const QString& trackId) const
    {
        QDir().mkpath(cacheDir());
        QFile file(cacheDir() + QLatin1Char('/') + Playback::TrackCache::keyFor(QStringLiteral("fake"), trackId));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(FakeUpstream::silence(30));
    }

    QString cacheDir() const { return temp_->filePath(QStringLiteral("tracks")); }

    // As App::Core does: a radio with no network goes on from the disk.
    void wireOfflineRadio()
    {
        playback_->setOfflineRadioProvider(
            [this](const QString& sourceId, int count) { return active_->cachedRadioTracks(sourceId, count); });
    }

    static Track track(const QString& id)
    {
        Track t;
        t.id = id;
        t.title = id;
        t.durationMs = 180000;
        return t;
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
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_, nullptr, cacheDir());
        session_ = std::make_unique<App::SourceSession>(
            *sourceManager_, *playback_, authStates_, trackStates_, coverArtCache_, messages_);
        editing_ = std::make_unique<App::PlaylistEditing>(*sourceManager_, messages_);
        sources_ = std::make_unique<ViewModel::Sources>(
            *sourceManager_, *session_, authStates_, *editing_, coverArtCache_, *settings_, messages_);
        playlistTracks_ = std::make_unique<App::PlaylistTrackCache>(temp_->filePath(QStringLiteral("playlist-tracks")));
        active_ = std::make_unique<ViewModel::ActivePlaylist>(*playback_, *sourceManager_, trackStates_, coverArtCache_,
            history_, *settings_, *sources_, *editing_, messages_, *playlistTracks_);
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
    std::unique_ptr<App::PlaylistTrackCache> playlistTracks_;
    std::unique_ptr<ViewModel::ActivePlaylist> active_;
};

QObject* makeActivePlaylistTest() { return new ActivePlaylistTest; }

} // namespace Tests

#include "ActivePlaylistTest.moc"
