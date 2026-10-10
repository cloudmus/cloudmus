#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <fileref.h>
#include <tag.h>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "DownloadPaths.h"
#include "Downloads.h"
#include "FakeBackend.h"
#include "FakeUpstream.h"
#include "Messages.h"
#include "PlaybackController.h"
#include "Preloader.h"
#include "ProxyRouting.h"
#include "RpcClient.h"
#include "Settings.h"
#include "SourceManager.h"
#include "SourceSession.h"
#include "TestSupport.h"
#include "TrackCache.h"
#include "TrackStates.h"

namespace Tests {

namespace {

// A few MPEG frames: audio as far as the front can tell, and tag-able.
QByteArray mp3()
{
    QByteArray data("ID3\x04\x00\x00\x00\x00\x00\x00", 10);
    QByteArray frame("\xff\xfb\x90\x00", 4);
    frame += QByteArray(417 - 4, '\0');
    for (int i = 0; i < 8; ++i)
        data += frame;
    return data;
}

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

private:
    // A track the front can fetch itself: its id is its stream's URL (see
    // FakeBackend's playback.resolveStream).
    Track streamTrack(const QString& name) const
    {
        Track t;
        t.id = upstream_->url(QLatin1Char('/') + name);
        t.title = QStringLiteral("Track %1").arg(name);
        return t;
    }

    QString savedFile(const Track& t) const
    {
        return Library::downloadDirectoryFor(
                   settings_.downloadDirectory(), settings_.downloadLayout(), QStringLiteral("Fake Source"), t)
            + QStringLiteral("/Unknown Artist - ") + t.title + QStringLiteral(".mp3");
    }

    void useThePlayersOwnDownloads() { downloads_->setPreloader(&playback_->preloader()); }

private slots:
    void initTestCase()
    {
        installFakeBackendManifest();
        Net::bypassProxyForLoopback();
        // Into the test-mode download location (QStandardPaths test mode).
        settings_.setDownloadDirectory(QDir::temp().filePath(QStringLiteral("cloudmus-tests-downloads")));
        settings_.setDownloadsEnabled(true);
    }

    void cleanupTestCase() { settings_.setDownloadsEnabled(false); }

    void init()
    {
        upstream_ = std::make_unique<FakeUpstream>();
        const QByteArray audio = mp3();
        for (const char* name : { "a", "b", "c" })
            upstream_->bodies.insert(QLatin1Char('/') + QLatin1String(name), audio);
        QDir(settings_.downloadDirectory()).removeRecursively();
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        playback_->setStreamRouteProvider([](const QString&) { return QNetworkProxy(QNetworkProxy::NoProxy); });
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
        upstream_.reset();
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

    // A track the front has downloaded for listening is saved from the
    // cache: no request to the source, nothing fetched again.
    void aTrackTheCacheHasIsSavedWithoutFetchingOrAskingAnyone()
    {
        useThePlayersOwnDownloads();
        const Track cached = streamTrack(QStringLiteral("a"));
        Playback::Preloader& preloader = playback_->preloader();
        preloader.setWindow(
            { Playback::Preloader::Item { QStringLiteral("fake"), cached.id, cached.title, std::nullopt, true } });
        QTRY_VERIFY(preloader.isCached(QStringLiteral("fake"), cached.id));
        const int fetched = upstream_->requests.size();

        QSignalSpy posted(&messages_, &ViewModel::Messages::posted);
        downloads_->downloadTrack(QStringLiteral("fake"), cached);
        QTRY_VERIFY(!downloads_->isActive());
        QCOMPARE(downloads_->jobs().first().state, ViewModel::Downloads::State::Done);
        QCOMPARE(upstream_->requests.size(), fetched);
        QCOMPARE(posted.last().at(1).toString(), QStringLiteral("Saved \"Track a\""));

        const QString saved = savedFile(cached);
        QVERIFY2(QFile::exists(saved), qPrintable(saved));
        QCOMPARE(QFileInfo(saved).fileName(), QStringLiteral("Unknown Artist - Track a.mp3"));
        TagLib::FileRef ref(QFile::encodeName(saved).constData());
        QVERIFY(!ref.isNull());
        QCOMPARE(QString::fromUtf8(ref.tag()->title().toCString(true)), QStringLiteral("Track a"));
        // The playback cache keeps what it had.
        QVERIFY(preloader.isCached(QStringLiteral("fake"), cached.id));
    }

    // One that isn't there is fetched by the front, with progress, and
    // doesn't stay in the playback cache afterwards.
    void aTrackThatIsNotInTheCacheIsFetchedByTheFrontAndSaved()
    {
        useThePlayersOwnDownloads();
        const Track wanted = streamTrack(QStringLiteral("d"));
        const QByteArray body = mp3() + QByteArray(300 * 1024, '\0');
        upstream_->bodies.insert(QStringLiteral("/d"), body);
        upstream_->maxAnswerBytes = 100 * 1024;
        upstream_->held.insert(QStringLiteral("/d"));

        downloads_->downloadTrack(QStringLiteral("fake"), wanted);
        QTRY_COMPARE(upstream_->requests.size(), 1);
        // The first part comes, the rest waits.
        upstream_->release(QStringLiteral("/d"));
        upstream_->held.insert(QStringLiteral("/d"));
        QTRY_COMPARE(downloads_->jobs().first().received, qint64(100 * 1024));
        QCOMPARE(downloads_->jobs().first().total, qint64(body.size()));
        QVERIFY(downloads_->jobs().first().progress() > 0.2);
        QVERIFY(downloads_->jobs().first().progress() < 0.4);

        upstream_->release(QStringLiteral("/d"));
        QTRY_VERIFY(!downloads_->isActive());
        QCOMPARE(downloads_->jobs().first().state, ViewModel::Downloads::State::Done);
        // The audio whole, and the tags on top of it.
        QVERIFY(QFileInfo(savedFile(wanted)).size() > qint64(body.size()));
        // Not kept in the playback cache, which it wasn't asked to be in.
        QVERIFY(!playback_->preloader().isCached(QStringLiteral("fake"), wanted.id));
        QCOMPARE(playback_->trackCache().totalBytes(), qint64(0));
    }

    void cancellingATrackTheFrontFetchesStopsItAndSavesNothing()
    {
        useThePlayersOwnDownloads();
        const Track wanted = streamTrack(QStringLiteral("c"));
        upstream_->held.insert(QStringLiteral("/c"));
        downloads_->downloadTrack(QStringLiteral("fake"), wanted);
        downloads_->downloadTrack(QStringLiteral("fake"), streamTrack(QStringLiteral("a")));
        const int first = downloads_->jobs().first().id;
        QTRY_COMPARE(upstream_->requests, QStringList { QStringLiteral("/c@0") });

        downloads_->cancel(first);
        // The queue goes on with the next track.
        QTRY_VERIFY(!downloads_->isActive());
        QCOMPARE(downloads_->jobs().at(0).state, ViewModel::Downloads::State::Cancelled);
        QCOMPARE(downloads_->jobs().at(1).state, ViewModel::Downloads::State::Done);
        QVERIFY(!QFile::exists(savedFile(wanted)));
        QVERIFY(QFile::exists(savedFile(streamTrack(QStringLiteral("a")))));
    }

    // When the stream can't be had, the source — which can also save — is
    // asked, as before.
    void aTrackWhoseStreamCantBeHadIsAskedOfTheSource()
    {
        useThePlayersOwnDownloads();
        QSignalSpy posted(&messages_, &ViewModel::Messages::posted);
        downloads_->downloadTrack(QStringLiteral("fake"), track(QStringLiteral("t1")));
        QTRY_VERIFY(!downloads_->isActive());
        QCOMPARE(downloads_->jobs().first().state, ViewModel::Downloads::State::Done);
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
    std::unique_ptr<FakeUpstream> upstream_;
    std::unique_ptr<Rpc::SourceManager> sourceManager_;
    std::unique_ptr<Playback::PlaybackController> playback_;
    std::unique_ptr<App::SourceSession> session_;
    std::unique_ptr<ViewModel::Downloads> downloads_;
};

QObject* makeDownloadsTest() { return new DownloadsTest; }

} // namespace Tests

#include "DownloadsTest.moc"
