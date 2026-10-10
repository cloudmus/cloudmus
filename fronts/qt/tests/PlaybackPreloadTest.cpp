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
