#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include "AuthStates.h"
#include "CoverArtCache.h"
#include "FakeBackend.h"
#include "FakeUpstream.h"
#include "Messages.h"
#include "PlaybackController.h"
#include "Preload.h"
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

// ViewModel::Preload: what a view is told about the tracks downloaded
// ahead, and what its settings do. Tracks are FakeUpstream's streams, as
// in PlaybackPreloadTest.
class PreloadTest : public QObject {
    Q_OBJECT

private:
    using Preload = ViewModel::Preload;

    Track track(const QString& name) const
    {
        Track t;
        t.id = upstream_->url(QLatin1Char('/') + name);
        t.title = name;
        return t;
    }

    void play(const QStringList& names)
    {
        QList<Track> tracks;
        for (const QString& name : names)
            tracks.append(track(name));
        playback_->loadQueue(QStringLiteral("fake"), tracks, 0);
    }

    Preload::State stateFor(const QString& name) const
    {
        return preload_->stateFor(QStringLiteral("fake"), track(name).id);
    }

    QVariant row(int row, int role) const
    {
        const QAbstractItemModel* model = preload_->upcoming();
        return model->data(model->index(row, 0), role);
    }

private slots:
    void initTestCase()
    {
        installFakeBackendManifest();
        Net::bypassProxyForLoopback();
    }

    void init()
    {
        // Tests named aSourcesOwn... run against a source without a network.
        qputenv(
            "CLOUDMUS_FAKE_NETWORK", QByteArray(QTest::currentTestFunction()).startsWith("aSourcesOwn") ? "0" : "1");
        // The defaults, whatever an earlier test or run left.
        settings_.setPreloadPlaylistTracks(3);
        settings_.setPreloadRadioTracks(2);
        settings_.setPreloadCacheMaxMb(1024);
        upstream_ = std::make_unique<FakeUpstream>();
        upstream_->bodies.insert(QStringLiteral("/a"), FakeUpstream::silence(30));
        for (const char* name : { "b", "c", "d" })
            upstream_->bodies.insert(QLatin1Char('/') + QLatin1String(name), FakeUpstream::pattern(2 * 1024 * 1024));
        sourceManager_ = std::make_unique<Rpc::SourceManager>();
        playback_ = std::make_unique<Playback::PlaybackController>(*sourceManager_);
        playback_->setVolume(0);
        playback_->setStreamRouteProvider([](const QString&) { return QNetworkProxy(QNetworkProxy::NoProxy); });
        session_ = std::make_unique<App::SourceSession>(
            *sourceManager_, *playback_, authStates_, trackStates_, coverArtCache_, messages_);
        preload_ = std::make_unique<Preload>(*playback_, settings_);
        QSignalSpy ready(session_.get(), &App::SourceSession::sourceReady);
        sourceManager_->startAll();
        QVERIFY(ready.wait(10000));
    }

    void cleanup()
    {
        for (Rpc::RpcClient* client : sourceManager_->clients())
            await(client->shutdown());
        preload_.reset();
        session_.reset();
        playback_.reset();
        sourceManager_.reset();
        upstream_.reset();
    }

    void theTracksAheadShowAsTheyDownload()
    {
        QCOMPARE(preload_->total(), 0);
        QVERIFY(preload_->summaryText().isEmpty());

        upstream_->held.insert(QStringLiteral("/b"));
        play({ QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") });
        // The playing track isn't one of them: the seek bar shows that one.
        QTRY_COMPARE(preload_->total(), 2);
        QCOMPARE(row(0, Preload::TitleRole).toString(), QStringLiteral("b"));
        QCOMPARE(row(1, Preload::TitleRole).toString(), QStringLiteral("c"));
        QTRY_COMPARE(stateFor(QStringLiteral("b")), Preload::State::Downloading);
        QCOMPARE(row(0, Preload::StateRole).value<Preload::State>(), Preload::State::Downloading);
        QCOMPARE(row(1, Preload::StateRole).value<Preload::State>(), Preload::State::Waiting);
        QCOMPARE(preload_->readyCount(), 0);
        QVERIFY(preload_->summaryText().startsWith(QStringLiteral("Downloaded ahead: 0 of 2")));
        QVERIFY(preload_->summaryText().contains(QStringLiteral("b — 0%")));
        QCOMPARE(row(1, Preload::DescriptionRole).toString(), QStringLiteral("c — waiting"));

        QSignalSpy tracks(preload_.get(), &Preload::trackChanged);
        upstream_->release(QStringLiteral("/b"));
        QTRY_COMPARE(preload_->readyCount(), 2);
        QCOMPARE(preload_->overallFraction(), 1.0);
        QCOMPARE(row(0, Preload::FractionRole).toDouble(), 1.0);
        QCOMPARE(row(0, Preload::DescriptionRole).toString(), QStringLiteral("b — downloaded"));
        QCOMPARE(stateFor(QStringLiteral("c")), Preload::State::Ready);
        QCOMPARE(preload_->fractionFor(QStringLiteral("fake"), track(QStringLiteral("c")).id), 1.0);
        // Each track's rows in a list were told to repaint.
        const auto told = [&](const QString& name) {
            return std::any_of(tracks.cbegin(), tracks.cend(),
                [&](const QList<QVariant>& args) { return args.at(1).toString() == track(name).id; });
        };
        QVERIFY(told(QStringLiteral("b")));
        QVERIFY(told(QStringLiteral("c")));
    }

    // A source without a network has files of its own: nothing downloads,
    // so there is nothing to show as preloaded.
    void aSourcesOwnFilesAreNotShownAsPreloaded()
    {
        const auto local = [](const QString& name) {
            Track t;
            t.id = QStringLiteral("file:///music/%1.mp3").arg(name);
            t.title = name;
            return t;
        };
        playback_->loadQueue(QStringLiteral("fake"),
            { local(QStringLiteral("a")), local(QStringLiteral("b")), local(QStringLiteral("c")) }, 0);
        // The playing track and the one after it are resolved: for a start
        // without a gap.
        QTRY_VERIFY(playback_->preloader().isLocal(QStringLiteral("fake"), local(QStringLiteral("b")).id));
        QTRY_VERIFY(playback_->preloader().isCached(QStringLiteral("fake"), local(QStringLiteral("b")).id) == false);
        QCOMPARE(preload_->total(), 0);
        QCOMPARE(preload_->upcoming()->rowCount(), 0);
        QVERIFY(preload_->summaryText().isEmpty());
        QCOMPARE(preload_->stateFor(QStringLiteral("fake"), local(QStringLiteral("b")).id), Preload::State::Waiting);
    }

    void theSettingsApplyAtOnceAndAreKept()
    {
        QCOMPARE(preload_->playlistTracks(), 3);
        QCOMPARE(preload_->radioTracks(), 2);
        QCOMPARE(preload_->cacheMaxMb(), 1024);

        QSignalSpy changed(preload_.get(), &Preload::settingsChanged);
        preload_->setPlaylistTracks(1);
        preload_->setRadioTracks(7);
        preload_->setCacheMaxMb(64);
        preload_->setCacheMaxMb(64); // nothing new
        QCOMPARE(changed.size(), 3);
        QCOMPARE(settings_.preloadPlaylistTracks(), 1);
        QCOMPARE(settings_.preloadRadioTracks(), 7);
        QCOMPARE(settings_.preloadCacheMaxMb(), 64);
        QCOMPARE(playback_->trackCache().maxBytes(), qint64(64) * 1024 * 1024);
        preload_->setPlaylistTracks(1000);
        QCOMPARE(preload_->playlistTracks(), Preload::kMaxTracks);

        // What's saved is what the next run starts with.
        settings_.setPreloadPlaylistTracks(1);
        Preload again(*playback_, settings_);
        QCOMPARE(again.playlistTracks(), 1);
        QCOMPARE(again.radioTracks(), 7);

        play({ QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") });
        QTRY_COMPARE(preload_->readyCount(), 1);
        QCOMPARE(preload_->total(), 1);
        preload_->setPlaylistTracks(2);
        QTRY_COMPARE(preload_->readyCount(), 2);
    }

    void clearingTheCacheKeepsWhatIsAboutToPlay()
    {
        preload_->setPlaylistTracks(1);
        play({ QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d") });
        QTRY_COMPARE(preload_->readyCount(), 1);
        // On to another queue: b and c are no longer wanted, just cached.
        play({ QStringLiteral("a"), QStringLiteral("d") });
        QTRY_COMPARE(preload_->readyCount(), 1);
        QTRY_VERIFY(playback_->preloader().isCached(QStringLiteral("fake"), track(QStringLiteral("a")).id));
        const qint64 before = preload_->cacheUsedBytes();
        QVERIFY(before >= 6 * 1024 * 1024);

        QSignalSpy used(preload_.get(), &Preload::cacheUsedChanged);
        preload_->clearCache();
        QVERIFY(used.size() >= 1);
        QCOMPARE(stateFor(QStringLiteral("c")), Preload::State::Waiting);
        QCOMPARE(stateFor(QStringLiteral("d")), Preload::State::Ready);
        QVERIFY(preload_->cacheUsedBytes() < before);
        QVERIFY(preload_->cacheUsedBytes() >= 2 * 1024 * 1024);
    }

    // Stepped: progress is told in quarters of a track — a few repaints per
    // track however many chunks it comes in.
    void steppedProgressComesInQuarters()
    {
        preload_->setProgressMode(Preload::ProgressMode::Stepped);
        preload_->setPlaylistTracks(1);
        upstream_->maxAnswerBytes = 64 * 1024; // b arrives in 32 pieces
        QList<double> fractions;
        connect(preload_->upcoming(), &QAbstractItemModel::dataChanged, this,
            [&]() { fractions.append(row(0, Preload::FractionRole).toDouble()); });
        QSignalSpy progress(preload_.get(), &Preload::progressChanged);
        play({ QStringLiteral("a"), QStringLiteral("b") });
        QTRY_COMPARE(preload_->readyCount(), 1);
        QVERIFY(upstream_->requests.size() > 30);
        for (double fraction : fractions)
            QVERIFY2(fraction == 0 || fraction == 0.25 || fraction == 0.5 || fraction == 0.75 || fraction == 1,
                qPrintable(QString::number(fraction)));
        QVERIFY(fractions.contains(0.5));
        // The window appearing, b starting, three quarters, b done — and
        // little else.
        QVERIFY2(progress.size() <= 10, qPrintable(QString::number(progress.size())));
        // As a whole it moves a track at a time.
        QCOMPARE(preload_->overallFraction(), 1.0);
    }

    // Smooth: as it goes, but no more often than a view can use.
    void smoothProgressIsHeldToAFewUpdatesASecond()
    {
        QCOMPARE(preload_->progressMode(), Preload::ProgressMode::Smooth);
        preload_->setPlaylistTracks(1);
        upstream_->maxAnswerBytes = 16 * 1024; // b arrives in 128 pieces
        QSignalSpy progress(preload_.get(), &Preload::progressChanged);
        QElapsedTimer timer;
        timer.start();
        play({ QStringLiteral("a"), QStringLiteral("b") });
        QTRY_COMPARE(preload_->readyCount(), 1);
        QVERIFY(upstream_->requests.size() > 100);
        const int allowed = 8 + int(timer.elapsed() / 250) * 2;
        QVERIFY2(progress.size() <= allowed, qPrintable(QString::number(progress.size())));

        QSignalSpy mode(preload_.get(), &Preload::progressModeChanged);
        preload_->setProgressMode(Preload::ProgressMode::Stepped);
        preload_->setProgressMode(Preload::ProgressMode::Stepped);
        QCOMPARE(mode.size(), 1);
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
    std::unique_ptr<Preload> preload_;
};

QObject* makePreloadTest() { return new PreloadTest; }

} // namespace Tests

#include "PreloadTest.moc"
