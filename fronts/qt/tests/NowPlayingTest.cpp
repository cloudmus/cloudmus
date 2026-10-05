#include <QSignalSpy>
#include <QTest>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "Downloads.h"
#include "FakeBackend.h"
#include "Messages.h"
#include "NowPlaying.h"
#include "PlaybackController.h"
#include "PlaybackHistory.h"
#include "ProxyRouting.h"
#include "RpcClient.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "TestSupport.h"
#include "TrackStates.h"

namespace Tests {

namespace {
Track track(const QString& id)
{
    Track t;
    t.id = id;
    t.title = QStringLiteral("Track %1").arg(id);
    return t;
}
} // namespace

class NowPlayingTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        installFakeBackendManifest();
        Net::bypassProxyForLoopback(); // as main() does, for the refused stream below
        settings_.setDownloadsEnabled(false); // the default, whatever an earlier run left
    }

    void init()
    {
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        trackStates_ = std::make_unique<Library::TrackStates>();
        session_ = std::make_unique<App::SourceSession>(
            *sourceManager_, *playback_, authStates_, *trackStates_, coverArtCache_, messages_);
        downloads_ = std::make_unique<ViewModel::Downloads>(
            *sourceManager_, *session_, *trackStates_, coverArtCache_, settings_, messages_);
        nowPlaying_ = std::make_unique<ViewModel::NowPlaying>(
            *playback_, *sourceManager_, *trackStates_, history_, settings_, *downloads_, messages_);
        QSignalSpy ready(sourceManager_.get(), &Rpc::SourceManager::sourceReady);
        sourceManager_->startAll();
        QVERIFY(ready.wait(10000));
    }

    void cleanup()
    {
        for (Rpc::RpcClient* client : sourceManager_->clients())
            await(client->shutdown());
        nowPlaying_.reset();
        downloads_.reset();
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
        trackStates_.reset();
    }

    // A stream refused although just resolved (YouTube's 403s) is resolved
    // again a couple of times; then the track is left paused, and Play
    // loads it anew instead of resuming nothing.
    void aTrackThatWontStartIsRetriedThenLeftForPlayToReload()
    {
        QSignalSpy loading(playback_.get(), &Playback::PlaybackController::loadingChanged);
        QSignalSpy errors(playback_.get(), &Playback::PlaybackController::errorOccurred);
        QSignalSpy failures(playback_.get(), &Playback::PlaybackController::failed);
        play({ track(QStringLiteral("refused")) });
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 20000);
        QCOMPARE(failures.size(), 1);
        QCOMPARE(failures.first().at(1).toString(), QStringLiteral("stream"));
        const auto starts = [&loading]() {
            return std::count_if(
                loading.cbegin(), loading.cend(), [](const QList<QVariant>& args) { return args.first().toBool(); });
        };
        QCOMPARE(starts(), 3);
        QVERIFY(!playback_->isPlaying());

        playback_->togglePause();
        QTRY_VERIFY(starts() >= 4);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 2, 20000);
    }

    void aTrackStartingShowsWithItsSourcesActions()
    {
        play({ track(QStringLiteral("t1")) });
        QCOMPARE(nowPlaying_->track().id, QStringLiteral("t1"));
        const auto feedback = nowPlaying_->feedback();
        QVERIFY(feedback.likeSupported);
        QVERIFY(feedback.dislikeSupported);
        QVERIFY(!feedback.downloadSupported); // downloads are off by default
        QVERIFY(!feedback.liked);
    }

    void aLikeShowsAsLikedAndBusyUntilTheSourceAnswers()
    {
        play({ track(QStringLiteral("t1")) });
        nowPlaying_->setLiked(true);
        QVERIFY(nowPlaying_->feedback().likeBusy);
        QVERIFY(nowPlaying_->feedback().liked); // the state asked for, while busy
        QTRY_VERIFY(!nowPlaying_->feedback().likeBusy);
        QVERIFY(nowPlaying_->feedback().liked);
        QCOMPARE(trackStates_->state(QStringLiteral("fake"), QStringLiteral("t1")).liked, std::optional<bool>(true));
    }

    void aSavedTrackKeepsItsLikeBeforeAndAfterPlaybackStarts()
    {
        Track saved = track(QStringLiteral("t1"));
        saved.liked = true;
        trackStates_->observe(QStringLiteral("fake"), saved);
        nowPlaying_->setPreviewTrack(QStringLiteral("fake"), saved.id);
        QVERIFY(!nowPlaying_->hasTrack());
        QVERIFY(nowPlaying_->feedback().likeSupported);
        QVERIFY(nowPlaying_->feedback().liked);

        play({ saved });
        QVERIFY(nowPlaying_->feedback().liked);
        QCOMPARE(trackStates_->state(QStringLiteral("fake"), saved.id).liked, std::optional<bool>(true));
    }

    void aFailedLikeRollsBackAndSaysWhy()
    {
        QSignalSpy messages(&messages_, &ViewModel::Messages::posted);
        play({ track(QStringLiteral("fail")) });
        nowPlaying_->setLiked(true);
        QTRY_VERIFY(!nowPlaying_->feedback().likeBusy);
        QVERIFY(!nowPlaying_->feedback().liked);
        QCOMPARE(messages.count(), 1);
        QCOMPARE(messages.first().at(1).toString(), QStringLiteral("Fake Source: service down"));
    }

    void aDislikeMovesOnToTheNextTrack()
    {
        play({ track(QStringLiteral("t1")), track(QStringLiteral("t2")) });
        nowPlaying_->setDisliked(true);
        QTRY_COMPARE(nowPlaying_->hasTrack() ? nowPlaying_->track().id : QString(), QStringLiteral("t2"));
        QCOMPARE(trackStates_->state(QStringLiteral("fake"), QStringLiteral("t1")).disliked, std::optional<bool>(true));
    }

private:
    void play(const QList<Track>& tracks)
    {
        QSignalSpy changed(nowPlaying_.get(), &ViewModel::NowPlaying::trackChanged);
        playback_->loadQueue(QStringLiteral("fake"), tracks, 0);
        QVERIFY(changed.wait(10000));
        QVERIFY(nowPlaying_->hasTrack());
    }

    Config::Settings settings_;
    History::PlaybackHistory history_;
    ViewModel::Messages messages_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<Playback::PlaybackController> playback_;
    std::unique_ptr<Library::TrackStates> trackStates_;
    Rpc::AuthStates authStates_;
    Covers::CoverArtCache coverArtCache_;
    std::unique_ptr<App::SourceSession> session_;
    std::unique_ptr<ViewModel::Downloads> downloads_;
    std::unique_ptr<ViewModel::NowPlaying> nowPlaying_;
};

QObject* makeNowPlayingTest() { return new NowPlayingTest; }

} // namespace Tests

#include "NowPlayingTest.moc"
