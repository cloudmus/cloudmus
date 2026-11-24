#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "Downloads.h"
#include "FakeBackend.h"
#include "Messages.h"
#include "NowPlaying.h"
#include "Outbox.h"
#include "PlaybackController.h"
#include "PlaybackHistory.h"
#include "RpcClient.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "TestSupport.h"
#include "TrackStates.h"

namespace Tests {

namespace {
QJsonObject trackParams(const QString& trackId) { return { { QStringLiteral("trackId"), trackId } }; }
}

class OutboxTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { installFakeBackendManifest(); }

    void init()
    {
        temp_ = std::make_unique<QTemporaryDir>();
        offlineFile_ = temp_->filePath(QStringLiteral("network-down"));
        qputenv("CLOUDMUS_FAKE_OFFLINE_FILE", offlineFile_.toUtf8());
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        QSignalSpy ready(sourceManager_.get(), &Rpc::SourceManager::sourceReady);
        sourceManager_->startAll();
        QVERIFY(ready.wait(10000));
        client_ = sourceManager_->client(QStringLiteral("fake"));
        QVERIFY(client_ != nullptr);
        outbox_ = makeOutbox();
    }

    void cleanup()
    {
        outbox_.reset();
        for (Rpc::RpcClient* client : sourceManager_->clients())
            await(client->shutdown());
        sourceManager_.reset();
        qunsetenv("CLOUDMUS_FAKE_OFFLINE_FILE");
        temp_.reset();
    }

    // Kept while offline, then sent in the order it was done — only the last
    // of a track's likes, and every play.
    void whatWaitsIsSentInOrderOnceOnline()
    {
        offline_ = true;
        outbox_->enqueue(QStringLiteral("fake"), QStringLiteral("feedback.like"), trackParams("a"), "rating:a");
        outbox_->enqueue(QStringLiteral("fake"), QStringLiteral("feedback.played"), trackParams("b"));
        outbox_->enqueue(QStringLiteral("fake"), QStringLiteral("feedback.unlike"), trackParams("a"), "rating:a");
        outbox_->enqueue(QStringLiteral("fake"), QStringLiteral("feedback.played"), trackParams("c"));
        QCOMPARE(outbox_->count(), 3);
        QVERIFY(outbox_->holds(QStringLiteral("fake")));
        QTest::qWait(50);
        QCOMPARE(received().size(), 0);

        offline_ = false;
        await(outbox_->flush(QStringLiteral("fake")));
        QCOMPARE(outbox_->count(), 0);
        QCOMPARE(received(),
            (QStringList { QStringLiteral("feedback.played b"), QStringLiteral("feedback.unlike a"),
                QStringLiteral("feedback.played c") }));
        QVERIFY(!outbox_->holds(QStringLiteral("fake")));
    }

    // What waits outlives the app.
    void itIsKeptOnDisk()
    {
        offline_ = true;
        outbox_->enqueue(QStringLiteral("fake"), QStringLiteral("feedback.dislike"), trackParams("x"), "rating:x");
        outbox_ = makeOutbox();
        QCOMPARE(outbox_->count(), 1);
        QCOMPARE(outbox_->items().first().method, QStringLiteral("feedback.dislike"));
        QCOMPARE(outbox_->items().first().key, QStringLiteral("rating:x"));
        offline_ = false;
        await(outbox_->flush(QStringLiteral("fake")));
        QCOMPARE(received(), QStringList { QStringLiteral("feedback.dislike x") });
        outbox_ = makeOutbox();
        QCOMPARE(outbox_->count(), 0);
    }

    // No network after all: it stops there, and keeps the rest.
    void aSendThatFindsNoNetworkStopsAndKeepsIt()
    {
        offline_ = true;
        outbox_->enqueue(QStringLiteral("fake"), QStringLiteral("feedback.like"), trackParams("a"), "rating:a");
        outbox_->enqueue(QStringLiteral("fake"), QStringLiteral("feedback.like"), trackParams("b"), "rating:b");
        offline_ = false;
        QFile(offlineFile_).open(QIODevice::WriteOnly);
        QSignalSpy seen(outbox_.get(), &App::Outbox::offlineSeen);
        await(outbox_->flush(QStringLiteral("fake")));
        QCOMPARE(seen.size(), 1);
        QCOMPARE(outbox_->count(), 2);
        QFile::remove(offlineFile_);
        await(outbox_->flush(QStringLiteral("fake")));
        QCOMPARE(outbox_->count(), 0);
        QCOMPARE(received(), (QStringList { QStringLiteral("feedback.like a"), QStringLiteral("feedback.like b") }));
    }

    // What the source refuses for good isn't tried again, and doesn't hold
    // up what follows.
    void aRefusalForGoodIsDropped()
    {
        offline_ = true;
        outbox_->enqueue(QStringLiteral("fake"), QStringLiteral("catalog.removeFromPlaylist"),
            { { QStringLiteral("playlistId"), QStringLiteral("p1") },
                { QStringLiteral("trackId"), QStringLiteral("fail") } },
            QStringLiteral("playlist:p1:fail"));
        outbox_->enqueue(QStringLiteral("fake"), QStringLiteral("feedback.like"), trackParams("a"), "rating:a");
        QSignalSpy dropped(outbox_.get(), &App::Outbox::dropped);
        offline_ = false;
        await(outbox_->flush(QStringLiteral("fake")));
        QCOMPARE(dropped.size(), 1);
        QCOMPARE(outbox_->count(), 0);
        QVERIFY(received().contains(QStringLiteral("feedback.like a")));
    }

    // A like with no network shows at once, and a listing from before it
    // was sent doesn't undo it.
    void aLikeOfflineIsShownAndHeld()
    {
        Library::TrackStates trackStates;
        Playback::PlaybackController playback(*sourceManager_);
        App::SourceSession session(*sourceManager_, playback, authStates_, trackStates, coverArtCache_, messages_);
        ViewModel::Downloads downloads(*sourceManager_, session, trackStates, coverArtCache_, settings_, messages_);
        ViewModel::NowPlaying nowPlaying(
            playback, *sourceManager_, trackStates, history_, settings_, downloads, messages_);
        nowPlaying.setOutbox(outbox_.get());
        Track track;
        track.id = QStringLiteral("t1");
        track.title = QStringLiteral("T1");
        track.durationMs = 1000;
        playback.loadQueue(QStringLiteral("fake"), { track }, 0);
        QTRY_VERIFY(nowPlaying.hasTrack());

        offline_ = true;
        nowPlaying.setLiked(true);
        QTRY_COMPARE(outbox_->count(), 1);
        QCOMPARE(trackStates.state(QStringLiteral("fake"), QStringLiteral("t1")).liked, std::optional<bool>(true));
        Track listed = track;
        listed.liked = false;
        trackStates.observe(QStringLiteral("fake"), listed);
        QCOMPARE(trackStates.state(QStringLiteral("fake"), QStringLiteral("t1")).liked, std::optional<bool>(true));

        offline_ = false;
        await(outbox_->flush(QStringLiteral("fake")));
        QCOMPARE(received(), QStringList { QStringLiteral("feedback.like t1") });
    }

private:
    std::unique_ptr<App::Outbox> makeOutbox()
    {
        outbox_.reset(); // saved by now: it saves at each change
        auto outbox = std::make_unique<App::Outbox>(*sourceManager_, temp_->filePath(QStringLiteral("outbox.json")));
        outbox->setOfflineProvider([this](const QString&) { return offline_; });
        return outbox;
    }

    // "method trackId" of each call the fake source got.
    QStringList received()
    {
        const QJsonObject result = await(client_->callRaw(QStringLiteral("fake.received"), { }, 5000));
        QStringList calls;
        for (const QJsonValue& call : result.value(QStringLiteral("calls")).toArray()) {
            const QJsonObject obj = call.toObject();
            calls.append(obj.value(QStringLiteral("method")).toString() + QLatin1Char(' ')
                + obj.value(QStringLiteral("params")).toObject().value(QStringLiteral("trackId")).toString());
        }
        return calls;
    }

    std::unique_ptr<QTemporaryDir> temp_;
    QString offlineFile_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    Rpc::RpcClient* client_ = nullptr;
    std::unique_ptr<App::Outbox> outbox_;
    bool offline_ = false;
    Rpc::AuthStates authStates_;
    Covers::CoverArtCache coverArtCache_;
    ViewModel::Messages messages_;
    Config::Settings settings_;
    History::PlaybackHistory history_;
};

QObject* makeOutboxTest() { return new OutboxTest; }

} // namespace Tests

#include "OutboxTest.moc"
