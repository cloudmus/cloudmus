#include <QDir>
#include <QSignalSpy>
#include <QTest>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "Downloads.h"
#include "FakeBackend.h"
#include "Messages.h"
#include "PlaybackController.h"
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

Playlist playlist(const QString& id)
{
    Playlist p;
    p.id = id;
    p.title = QStringLiteral("Playlist %1").arg(id);
    p.kind = QStringLiteral("playlist");
    return p;
}
} // namespace

class DownloadsTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        installFakeBackendManifest();
        // Into the test-mode download location (QStandardPaths test mode).
        settings_.setDownloadDirectory(QDir::temp().filePath(QStringLiteral("cloudmus-tests-downloads")));
        settings_.setDownloadsEnabled(true);
    }

    void cleanupTestCase() { settings_.setDownloadsEnabled(false); }

    void init()
    {
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        session_ = std::make_unique<App::SourceSession>(
            *sourceManager_, *playback_, authStates_, trackStates_, coverArtCache_, messages_);
        downloads_ = std::make_unique<ViewModel::Downloads>(
            *sourceManager_, *session_, trackStates_, coverArtCache_, settings_, messages_);
        QSignalSpy ready(session_.get(), &App::SourceSession::sourceReady);
        sourceManager_->startAll();
        QVERIFY(ready.wait(10000));
    }

    void cleanup()
    {
        for (Rpc::RpcClient* client : sourceManager_->clients())
            await(client->shutdown());
        downloads_.reset();
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
    }

    void aTrackIsSavedWithItsProgressAndSaidSo()
    {
        QSignalSpy posted(&messages_, &ViewModel::Messages::posted);
        QList<qint64> received;
        connect(downloads_.get(), &ViewModel::Downloads::changed, this, [&]() {
            const auto& jobs = downloads_->jobs();
            if (!jobs.isEmpty() && jobs.first().total > 0)
                received.append(jobs.first().received);
        });
        downloads_->downloadTrack(QStringLiteral("fake"), track(QStringLiteral("t1")));
        QVERIFY(downloads_->isActive());
        QVERIFY(downloads_->isDownloading(QStringLiteral("fake"), QStringLiteral("t1")));
        QTRY_VERIFY(!downloads_->isActive());
        QCOMPARE(downloads_->jobs().first().state, ViewModel::Downloads::State::Done);
        QVERIFY(received.contains(50));
        QCOMPARE(posted.last().at(1).toString(), QStringLiteral("Saved \"Track t1\""));
    }

    void aPlaylistIsSavedTrackByTrack()
    {
        QSignalSpy posted(&messages_, &ViewModel::Messages::posted);
        downloads_->downloadPlaylist(QStringLiteral("fake"), playlist(QStringLiteral("p1")));
        QTRY_VERIFY(!downloads_->isActive());
        const ViewModel::Downloads::Job& job = downloads_->jobs().first();
        QCOMPARE(job.state, ViewModel::Downloads::State::Done);
        QCOMPARE(job.saved, 2);
        QCOMPARE(job.progress(), 1.0);
        QCOMPARE(posted.last().at(1).toString(), QStringLiteral("Saved 2 tracks of \"Playlist p1\""));
    }

    void cancellingStopsTheTrackUnderWayAndTheQueueGoesOn()
    {
        downloads_->downloadTrack(QStringLiteral("fake"), track(QStringLiteral("slow")));
        downloads_->downloadTrack(QStringLiteral("fake"), track(QStringLiteral("t2")));
        const int slow = downloads_->jobs().first().id;
        // Under way, halfway through.
        QTRY_COMPARE(downloads_->jobs().first().received, qint64(50));
        QCOMPARE(downloads_->jobs().first().progress(), 0.5);
        downloads_->cancel(slow);
        QCOMPARE(downloads_->jobs().first().state, ViewModel::Downloads::State::Cancelled);
        QTRY_VERIFY(!downloads_->isActive());
        QCOMPARE(downloads_->jobs().first().state, ViewModel::Downloads::State::Cancelled);
        QCOMPARE(downloads_->jobs().last().state, ViewModel::Downloads::State::Done);
    }

    void aFailedTrackIsSaidSo()
    {
        QSignalSpy posted(&messages_, &ViewModel::Messages::posted);
        downloads_->downloadTrack(QStringLiteral("fake"), track(QStringLiteral("fail")));
        QTRY_VERIFY(!downloads_->isActive());
        QCOMPARE(downloads_->jobs().first().state, ViewModel::Downloads::State::Failed);
        QCOMPARE(posted.last().at(0).value<ViewModel::Messages::Kind>(), ViewModel::Messages::Kind::Error);
        QCOMPARE(posted.last().at(1).toString(), QStringLiteral("Fake Source: no such track"));
    }

    void nothingStartsWhileDownloadsAreOff()
    {
        downloads_->setEnabled(false);
        downloads_->downloadTrack(QStringLiteral("fake"), track(QStringLiteral("t1")));
        QVERIFY(downloads_->jobs().isEmpty());
        downloads_->setEnabled(true);
    }

    void finishedJobsCanBeCleared()
    {
        downloads_->downloadTrack(QStringLiteral("fake"), track(QStringLiteral("t1")));
        QTRY_VERIFY(!downloads_->isActive());
        downloads_->clearFinished();
        QVERIFY(downloads_->jobs().isEmpty());
    }

private:
    Config::Settings settings_;
    Rpc::AuthStates authStates_;
    Library::TrackStates trackStates_;
    Covers::CoverArtCache coverArtCache_;
    ViewModel::Messages messages_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<Playback::PlaybackController> playback_;
    std::unique_ptr<App::SourceSession> session_;
    std::unique_ptr<ViewModel::Downloads> downloads_;
};

QObject* makeDownloadsTest() { return new DownloadsTest; }

} // namespace Tests

#include "DownloadsTest.moc"
