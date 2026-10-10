#include <QSignalSpy>
#include <QTest>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "FakeBackend.h"
#include "FakeUpstream.h"
#include "Messages.h"
#include "PlaybackController.h"
#include "Preloader.h"
#include "ProxyRouting.h"
#include "RpcClient.h"
#include "TrackCache.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "TestSupport.h"
#include "TrackStates.h"

namespace Tests {

// PlaybackController and its Preloader together: what gets downloaded as a
// queue plays, and what the downloaded tracks change about playing it. The
// fake backend's tracks here are the streams FakeUpstream serves — a
// track's id is its URL.
class PlaybackPreloadTest : public QObject {
    Q_OBJECT

private:
    Track track(const QString& name) const
    {
        Track t;
        t.id = upstream_->url(QLatin1Char('/') + name);
        t.title = name;
        t.durationMs = 30000; // what FakeUpstream::silence(30) plays for
        return t;
    }

    bool cached(const QString& name) const
    {
        return playback_->preloader().isCached(QStringLiteral("fake"), track(name).id);
    }

    QStringList queueNames() const
    {
        QStringList names;
        for (const Playback::QueueEntry& entry : playback_->queue())
            names.append(entry.track.title);
        return names;
    }

private slots:
    void initTestCase()
    {
        installFakeBackendManifest();
        Net::bypassProxyForLoopback();
    }

    void init()
    {
        // Tests named aLocal... run against a source without a network.
        qputenv("CLOUDMUS_FAKE_NETWORK", QByteArray(QTest::currentTestFunction()).startsWith("aLocal") ? "0" : "1");
        upstream_ = std::make_unique<FakeUpstream>();
        for (const char* name : { "a", "b", "c", "d" })
            upstream_->bodies.insert(QLatin1Char('/') + QLatin1String(name), FakeUpstream::silence(30));
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        playback_->setVolume(0);
        playback_->setStreamRouteProvider([](const QString&) { return QNetworkProxy(QNetworkProxy::NoProxy); });
        session_ = std::make_unique<App::SourceSession>(
            *sourceManager_, *playback_, authStates_, trackStates_, coverArtCache_, messages_);
        QSignalSpy ready(session_.get(), &App::SourceSession::sourceReady);
        sourceManager_->startAll();
        QVERIFY(ready.wait(10000));
    }

    void cleanup()
    {
        for (Rpc::RpcClient* client : sourceManager_->clients())
            await(client->shutdown());
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
        upstream_.reset();
    }

    void theNextTrackIsNotFetchedBeforeThePlayingOneIsThere()
    {
        upstream_->held.insert(QStringLiteral("/a"));
        playback_->loadQueue(QStringLiteral("fake"), { track(QStringLiteral("a")), track(QStringLiteral("b")) }, 0);
        QTRY_COMPARE(upstream_->requests, QStringList { QStringLiteral("/a@0") });
        QTest::qWait(200);
        QCOMPARE(upstream_->requests.size(), 1);

        upstream_->release(QStringLiteral("/a"));
        QTRY_VERIFY(cached(QStringLiteral("b")));
        QCOMPARE(upstream_->requests.mid(0, 2), (QStringList { QStringLiteral("/a@0"), QStringLiteral("/b@0") }));
    }

    void onlySoManyTracksAreFetchedAhead()
    {
        playback_->setPreloadDepth(1, 0);
        playback_->loadQueue(QStringLiteral("fake"),
            { track(QStringLiteral("a")), track(QStringLiteral("b")), track(QStringLiteral("c")) }, 0);
        QTRY_VERIFY(cached(QStringLiteral("b")));
        QTest::qWait(200);
        QVERIFY(!cached(QStringLiteral("c")));

        // More wanted: the rest follows.
        playback_->setPreloadDepth(2, 0);
        QTRY_VERIFY(cached(QStringLiteral("c")));
    }

    // No round trip to the source: the track is announced before the
    // event loop has turned once.
    void aDownloadedTrackStartsWithoutAskingTheSource()
    {
        playback_->loadQueue(QStringLiteral("fake"), { track(QStringLiteral("a")), track(QStringLiteral("b")) }, 0);
        QTRY_VERIFY(cached(QStringLiteral("b")));
        QTRY_VERIFY(playback_->hasCurrentTrack());

        QSignalSpy changed(playback_.get(), &Playback::PlaybackController::trackChanged);
        playback_->next();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(playback_->currentTrack().title, QStringLiteral("b"));
        QCOMPARE(upstream_->requests.count(QStringLiteral("/b@0")), 1);
    }

    // The mark on the seek bar is how much of the track is downloaded —
    // not how far ahead of the playhead mpv happens to have read.
    void theSeekBarShowsHowMuchIsDownloaded()
    {
        QSignalSpy buffered(playback_.get(), &Playback::PlaybackController::bufferedChanged);
        const auto last = [&buffered]() { return buffered.isEmpty() ? -2 : buffered.last().first().toLongLong(); };
        // Half of it arrives, and then no more.
        upstream_->stallAfter.insert(QStringLiteral("/a"), upstream_->bodies.value(QStringLiteral("/a")).size() / 2);
        playback_->loadQueue(QStringLiteral("fake"), { track(QStringLiteral("a")) }, 0);
        QTRY_VERIFY2(last() > 14000 && last() < 16000, qPrintable(QString::number(last())));
        QTest::qWait(500); // it plays on meanwhile
        QVERIFY2(last() > 14000 && last() < 16000, qPrintable(QString::number(last())));

        // All of it there: the whole bar.
        playback_->loadQueue(QStringLiteral("fake"), { track(QStringLiteral("b")), track(QStringLiteral("c")) }, 0);
        QTRY_COMPARE(last(), qint64(30000));
        QTRY_VERIFY(cached(QStringLiteral("c")));
        QTest::qWait(300);
        QCOMPARE(last(), qint64(30000));

        // A track played from the cache has it from the start.
        buffered.clear();
        playback_->next();
        QCOMPARE(last(), qint64(30000));
        QTest::qWait(500);
        QCOMPARE(last(), qint64(30000));
    }

    // A source's own files take nothing of the preload depth and are not
    // fetched — only the next one is kept ready for a start without a gap.
    void aLocalSourcesNextTrackIsPreparedAndNoMoreIsPreloaded()
    {
        playback_->setPreloadDepth(3, 3);
        QList<Track> tracks;
        for (const char* name : { "a", "b", "c", "d", "e" }) {
            Track t;
            t.id = QStringLiteral("file:///music/%1.mp3").arg(QLatin1String(name));
            t.title = QLatin1String(name);
            tracks.append(t);
        }
        playback_->loadQueue(QStringLiteral("fake"), tracks, 0);
        QTRY_VERIFY(playback_->preloader().isLocal(QStringLiteral("fake"), tracks.at(1).id));
        QTRY_VERIFY(!playback_->preloader().readyPath(QStringLiteral("fake"), tracks.at(1).id).isEmpty());
        QTest::qWait(200);
        // Not the ones after it.
        QVERIFY(!playback_->preloader().isLocal(QStringLiteral("fake"), tracks.at(2).id));
        QCOMPARE(playback_->preloader().window().size(), 2);
        // Nothing was fetched from anywhere.
        QCOMPARE(upstream_->requests.size(), 0);
        QCOMPARE(playback_->trackCache().totalBytes(), qint64(0));
    }

    // A skip makes a radio replace what's coming — after what's already
    // downloaded, which is there to be played.
    void aRadioReplacingItsUpcomingTracksKeepsTheDownloadedOnes()
    {
        playback_->setPreloadDepth(3, 2);
        playback_->startRadio(QStringLiteral("fake"), QStringLiteral("station"),
            { track(QStringLiteral("a")), track(QStringLiteral("b")), track(QStringLiteral("c")),
                track(QStringLiteral("d")) });
        QTRY_VERIFY(cached(QStringLiteral("c")));
        QTRY_VERIFY(playback_->hasCurrentTrack());
        QTest::qWait(200);
        QVERIFY(!cached(QStringLiteral("d"))); // a radio's own depth

        TracksAddedParams added;
        added.stationId = QStringLiteral("station");
        added.tracks = { track(QStringLiteral("b")), track(QStringLiteral("x")), track(QStringLiteral("y")) };
        added.replaceUpcoming = true;
        playback_->handleTracksAdded(QStringLiteral("fake"), added);
        QCOMPARE(queueNames(),
            (QStringList { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("x"),
                QStringLiteral("y") }));
    }

    // However much of the queue the cache holds, a replacement lands no
    // further ahead than tracks are preloaded.
    void aRadioKeepsNoMoreDownloadedTracksThanItPreloads()
    {
        playback_->setPreloadDepth(3, 3);
        playback_->startRadio(QStringLiteral("fake"), QStringLiteral("station"),
            { track(QStringLiteral("a")), track(QStringLiteral("b")), track(QStringLiteral("c")),
                track(QStringLiteral("d")) });
        QTRY_VERIFY(cached(QStringLiteral("d")));
        QTRY_VERIFY(playback_->hasCurrentTrack());

        // Set lower since: b, c and d are all there, but only one is wanted.
        playback_->setPreloadDepth(3, 1);
        TracksAddedParams added;
        added.stationId = QStringLiteral("station");
        added.tracks = { track(QStringLiteral("x")) };
        added.replaceUpcoming = true;
        playback_->handleTracksAdded(QStringLiteral("fake"), added);
        QCOMPARE(queueNames(), (QStringList { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("x") }));
    }

    // A radio replaces what's queued when the listener rejects the playing
    // track — Next. Picking another track of the queue, ahead or behind,
    // and going back are not that: the queue stays as it is.
    void onlyNextMakesARadioReplaceItsQueue()
    {
        playback_->setPreloadDepth(0, 0); // nothing downloaded ahead to be kept for that reason
        const QStringList queued { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d") };
        playback_->startRadio(QStringLiteral("fake"), QStringLiteral("station"),
            { track(QStringLiteral("a")), track(QStringLiteral("b")), track(QStringLiteral("c")),
                track(QStringLiteral("d")) });
        QTRY_VERIFY(playback_->hasCurrentTrack());

        playback_->playAt(2);
        QTRY_COMPARE(playback_->currentIndex(), 2);
        playback_->previous();
        QTRY_COMPARE(playback_->currentIndex(), 1);
        playback_->playAt(0);
        QTRY_COMPARE(playback_->currentIndex(), 0);
        QTest::qWait(300); // time for a replacement to arrive, had one been asked for
        QCOMPARE(queueNames(), queued);

        // "Not this": the station's new pick replaces what was queued after
        // b, which comes next. c is in the cache (it played a moment ago),
        // but nothing is preloaded ahead here, so it isn't held on to.
        playback_->next();
        QTRY_COMPARE(
            queueNames(), (QStringList { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("after-skip-1") }));
        QCOMPARE(playback_->currentIndex(), 1);
    }

private:
    Rpc::AuthStates authStates_;
    Library::TrackStates trackStates_;
    Covers::CoverArtCache coverArtCache_;
    ViewModel::Messages messages_;
    std::unique_ptr<FakeUpstream> upstream_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<Playback::PlaybackController> playback_;
    std::unique_ptr<App::SourceSession> session_;
};

QObject* makePlaybackPreloadTest() { return new PlaybackPreloadTest; }

} // namespace Tests

#include "PlaybackPreloadTest.moc"
