#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>

#include "FakeUpstream.h"
#include "Preloader.h"
#include "ProxyRouting.h"
#include "StreamDownload.h"
#include "TrackCache.h"

namespace Tests {

namespace {

Rpc::Task<StreamDescriptor> resolved(StreamDescriptor stream) { co_return stream; }

} // namespace

class PreloaderTest : public QObject {
    Q_OBJECT

private:
    using Item = Playback::Preloader::Item;
    using State = Playback::Preloader::State;

    StreamDescriptor stream(const QString& path) const
    {
        StreamDescriptor descriptor;
        descriptor.kind = QStringLiteral("url");
        descriptor.url = upstream_->url(path);
        descriptor.mimeType = QStringLiteral("audio/mpeg");
        return descriptor;
    }

    // The playing track: its stream is known.
    Item playing(const QString& id) const
    {
        return Item { QStringLiteral("src"), id, id, stream(QLatin1Char('/') + id), false };
    }
    // An upcoming one: resolved when its turn comes.
    static Item upcoming(const QString& id) { return Item { QStringLiteral("src"), id, id, std::nullopt, true }; }

    State stateOf(const QString& id) const { return preloader_->stateOf(QStringLiteral("src"), id); }

    QByteArray cached(const QString& id) const
    {
        QFile file(preloader_->readyPath(QStringLiteral("src"), id));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private slots:
    void initTestCase() { Net::bypassProxyForLoopback(); }

    void init()
    {
        dir_ = std::make_unique<QTemporaryDir>();
        upstream_ = std::make_unique<FakeUpstream>();
        for (const char* id : { "a", "b", "c" })
            upstream_->bodies.insert(QLatin1Char('/') + QLatin1String(id), FakeUpstream::pattern(300 * 1024, id[0]));
        cache_ = std::make_unique<Playback::TrackCache>(dir_->path());
        preloader_ = std::make_unique<Playback::Preloader>(*cache_);
        resolveCalls_.clear();
        preloader_->setRouteProvider([](const QString&) { return QNetworkProxy(QNetworkProxy::NoProxy); });
        preloader_->setResolver(
            [this](const QString&, const QString& trackId) -> std::optional<Playback::Preloader::Resolution> {
                resolveCalls_.append(trackId);
                if (unresolvable_.contains(trackId))
                    return std::nullopt;
                const QString path = freshPaths_.value(trackId, QLatin1Char('/') + trackId);
                return Playback::Preloader::Resolution { resolved(stream(path)), { } };
            });
        unresolvable_.clear();
        freshPaths_.clear();
    }

    void cleanup()
    {
        preloader_.reset();
        cache_.reset();
        upstream_.reset();
        dir_.reset();
    }

    // The point of it all: nothing takes the link from the track the
    // listener is waiting for.
    void tracksDownloadOneAtATimeInPlayOrder()
    {
        upstream_->held.insert(QStringLiteral("/a"));
        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        preloader_->setWindow(
            { playing(QStringLiteral("a")), upcoming(QStringLiteral("b")), upcoming(QStringLiteral("c")) });
        QTRY_COMPARE(upstream_->requests, QStringList { QStringLiteral("/a@0") });
        QTest::qWait(150);
        QCOMPARE(upstream_->requests.size(), 1);
        QVERIFY(resolveCalls_.isEmpty()); // not even resolved: URLs don't keep
        QCOMPARE(stateOf(QStringLiteral("a")), State::Downloading);
        QCOMPARE(stateOf(QStringLiteral("b")), State::Waiting);

        upstream_->release(QStringLiteral("/a"));
        QTRY_COMPARE(ready.size(), 3);
        QCOMPARE(upstream_->requests,
            (QStringList { QStringLiteral("/a@0"), QStringLiteral("/b@0"), QStringLiteral("/c@0") }));
        QCOMPARE(resolveCalls_, (QStringList { QStringLiteral("b"), QStringLiteral("c") }));
        for (const char* id : { "a", "b", "c" }) {
            QCOMPARE(stateOf(QLatin1String(id)), State::Ready);
            QVERIFY(preloader_->isCached(QStringLiteral("src"), QLatin1String(id)));
            QCOMPARE(cached(QLatin1String(id)), upstream_->bodies.value(QLatin1Char('/') + QLatin1String(id)));
        }
        const auto window = preloader_->window();
        QCOMPARE(window.size(), 3);
        QCOMPARE(window.at(2).fraction, 1.0);
    }

    // Fetched in ranges, one right after another.
    void aTrackComesInRanges()
    {
        upstream_->maxAnswerBytes = 100 * 1024;
        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        preloader_->setWindow({ playing(QStringLiteral("a")) });
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(upstream_->requests,
            (QStringList { QStringLiteral("/a@0"), QStringLiteral("/a@102400"), QStringLiteral("/a@204800") }));
        QCOMPARE(cached(QStringLiteral("a")), upstream_->bodies.value(QStringLiteral("/a")));
    }

    void untilThePlayingTracksStreamIsKnownNothingDownloads()
    {
        Item starting = playing(QStringLiteral("a"));
        starting.stream.reset();
        preloader_->setWindow({ starting, upcoming(QStringLiteral("b")) });
        QTest::qWait(150);
        QVERIFY(upstream_->requests.isEmpty());
        QVERIFY(resolveCalls_.isEmpty());

        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        preloader_->setWindow({ playing(QStringLiteral("a")), upcoming(QStringLiteral("b")) });
        QTRY_COMPARE(ready.size(), 2);
        QCOMPARE(upstream_->requests, (QStringList { QStringLiteral("/a@0"), QStringLiteral("/b@0") }));
    }

    void aTrackLeavingTheWindowStopsDownloading()
    {
        upstream_->stallAfter.insert(QStringLiteral("/a"), 100 * 1024);
        preloader_->setWindow({ playing(QStringLiteral("a")) });
        const QString part = cache_->partPath(Playback::TrackCache::keyFor(QStringLiteral("src"), QStringLiteral("a")));
        QTRY_VERIFY(QFileInfo(part).size() >= 100 * 1024);

        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        preloader_->setWindow({ playing(QStringLiteral("b")) });
        QVERIFY(!QFileInfo::exists(part));
        QCOMPARE(stateOf(QStringLiteral("a")), State::Waiting);
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(!preloader_->isCached(QStringLiteral("src"), QStringLiteral("a")));
        QVERIFY(preloader_->isCached(QStringLiteral("src"), QStringLiteral("b")));
    }

    // Next pressed while the track after it was coming: the new playing
    // track goes first, the other one keeps what it had.
    void aTrackThatMustPlayNowGoesFirstAndTheOtherResumes()
    {
        upstream_->stallAfter.insert(QStringLiteral("/b"), 100 * 1024);
        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        preloader_->setWindow({ playing(QStringLiteral("a")), upcoming(QStringLiteral("b")) });
        QTRY_COMPARE(ready.size(), 1);
        Playback::StreamDownload* download = nullptr;
        QTRY_VERIFY((download = preloader_->download(QStringLiteral("src"), QStringLiteral("b"))) != nullptr);
        QTRY_COMPARE(download->bytesAvailable(), qint64(100 * 1024));

        upstream_->held.insert(QStringLiteral("/c"));
        preloader_->setWindow({ playing(QStringLiteral("c")), upcoming(QStringLiteral("b")) });
        QCOMPARE(stateOf(QStringLiteral("b")), State::Waiting);
        QCOMPARE(stateOf(QStringLiteral("c")), State::Downloading);
        QTRY_COMPARE(upstream_->requests.last(), QStringLiteral("/c@0"));
        QTest::qWait(150);
        QCOMPARE(upstream_->requests.last(), QStringLiteral("/c@0"));

        upstream_->release(QStringLiteral("/c"));
        QTRY_COMPARE(ready.size(), 3);
        QCOMPARE(upstream_->requests.last(), QStringLiteral("/b@102400"));
        QCOMPARE(cached(QStringLiteral("b")), upstream_->bodies.value(QStringLiteral("/b")));
        QCOMPARE(resolveCalls_, QStringList { QStringLiteral("b") }); // not resolved a second time
    }

    void aUrlThatExpiredIsResolvedAgain()
    {
        upstream_->forbidden.insert(QStringLiteral("/b"));
        upstream_->bodies.insert(QStringLiteral("/b-fresh"), upstream_->bodies.value(QStringLiteral("/b")));
        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        QSignalSpy failed(preloader_.get(), &Playback::Preloader::failed);
        Item item = playing(QStringLiteral("b"));
        freshPaths_.insert(QStringLiteral("b"), QStringLiteral("/b-fresh"));
        preloader_->setWindow({ item });
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(upstream_->requests, (QStringList { QStringLiteral("/b@0"), QStringLiteral("/b-fresh@0") }));
        QCOMPARE(cached(QStringLiteral("b")), upstream_->bodies.value(QStringLiteral("/b")));
    }

    void aTrackThatCantBeHadIsSkipped()
    {
        upstream_->forbidden.insert(QStringLiteral("/a")); // and stays so, however often resolved
        unresolvable_.insert(QStringLiteral("b"));
        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        QSignalSpy failed(preloader_.get(), &Playback::Preloader::failed);
        preloader_->setWindow(
            { playing(QStringLiteral("a")), upcoming(QStringLiteral("b")), upcoming(QStringLiteral("c")) });
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(failed.size(), 2);
        QCOMPARE(stateOf(QStringLiteral("a")), State::Failed);
        QCOMPARE(stateOf(QStringLiteral("b")), State::Failed);
        QCOMPARE(stateOf(QStringLiteral("c")), State::Ready);

        // A stream resolved anew is another chance.
        upstream_->forbidden.clear();
        preloader_->setWindow({ playing(QStringLiteral("a")), upcoming(QStringLiteral("c")) });
        QTRY_COMPARE(stateOf(QStringLiteral("a")), State::Ready);
    }

    void aLocalFileNeedsNoDownload()
    {
        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        Item item = playing(QStringLiteral("a"));
        item.stream->url = QUrl::fromLocalFile(dir_->filePath(QStringLiteral("song.mp3"))).toString();
        preloader_->setWindow({ item, upcoming(QStringLiteral("b")) });
        QCOMPARE(stateOf(QStringLiteral("a")), State::Ready);
        QCOMPARE(preloader_->readyPath(QStringLiteral("src"), QStringLiteral("a")),
            dir_->filePath(QStringLiteral("song.mp3")));
        QVERIFY(!preloader_->isCached(QStringLiteral("src"), QStringLiteral("a")));
        QTRY_COMPARE(ready.size(), 2);
        QCOMPARE(upstream_->requests, QStringList { QStringLiteral("/b@0") });
    }

    void whatIsCachedIsReadyAtOnce()
    {
        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        preloader_->setWindow({ playing(QStringLiteral("a")) });
        QTRY_COMPARE(ready.size(), 1);
        preloader_->setWindow({ });
        QCOMPARE(stateOf(QStringLiteral("a")), State::Ready); // still cached, wanted or not

        upstream_->requests.clear();
        preloader_->setWindow({ upcoming(QStringLiteral("a")) });
        QCOMPARE(stateOf(QStringLiteral("a")), State::Ready);
        QTest::qWait(50);
        QVERIFY(upstream_->requests.isEmpty());

        // Unplayable after all: gone, to be downloaded anew.
        preloader_->discard(QStringLiteral("src"), QStringLiteral("a"));
        QVERIFY(!preloader_->isCached(QStringLiteral("src"), QStringLiteral("a")));
        QCOMPARE(stateOf(QStringLiteral("a")), State::Waiting);
    }

    // Saving is fetched after what plays and before what is only preloaded.
    void aDemandedTrackGoesAfterThePlayingOneAndBeforeTheWindow()
    {
        upstream_->held.insert(QStringLiteral("/a"));
        QStringList readied;
        connect(preloader_.get(), &Playback::Preloader::ready, this,
            [&](const QString&, const QString& id) { readied.append(id); });
        preloader_->setWindow({ playing(QStringLiteral("a")), upcoming(QStringLiteral("b")) });
        QTRY_COMPARE(upstream_->requests, QStringList { QStringLiteral("/a@0") });

        // Asked for while a is still coming, c is not part of the window.
        preloader_->demand(upcoming(QStringLiteral("c")));
        QTest::qWait(100);
        QCOMPARE(upstream_->requests.size(), 1); // still one at a time

        upstream_->release(QStringLiteral("/a"));
        QTRY_COMPARE(readied.size(), 3);
        QCOMPARE(readied, (QStringList { QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b") }));
        QCOMPARE(preloader_->transferOf(QStringLiteral("src"), QStringLiteral("c")).received,
            upstream_->bodies.value(QStringLiteral("/c")).size());
    }

    // Once saved, a track nobody plays leaves the cache; one that is part
    // of the window stays.
    void aReleasedTrackLeavesTheCacheUnlessItIsInTheWindow()
    {
        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        preloader_->setWindow({ playing(QStringLiteral("a")) });
        preloader_->demand(upcoming(QStringLiteral("b")));
        preloader_->demand(upcoming(QStringLiteral("a"))); // the playing one, too
        QTRY_COMPARE(ready.size(), 2);
        QVERIFY(preloader_->isCached(QStringLiteral("src"), QStringLiteral("b")));

        preloader_->release(QStringLiteral("src"), QStringLiteral("b"));
        QVERIFY(!preloader_->isCached(QStringLiteral("src"), QStringLiteral("b")));
        QCOMPARE(cache_->totalBytes(), qint64(upstream_->bodies.value(QStringLiteral("/a")).size()));
        preloader_->release(QStringLiteral("src"), QStringLiteral("a"));
        QVERIFY(preloader_->isCached(QStringLiteral("src"), QStringLiteral("a")));
    }

    void aDemandCanBeCancelledWhileItDownloads()
    {
        upstream_->stallAfter.insert(QStringLiteral("/b"), 100 * 1024);
        preloader_->demand(upcoming(QStringLiteral("b")));
        const QString part = cache_->partPath(Playback::TrackCache::keyFor(QStringLiteral("src"), QStringLiteral("b")));
        QTRY_VERIFY(QFileInfo(part).size() >= 100 * 1024);

        preloader_->release(QStringLiteral("src"), QStringLiteral("b"));
        QVERIFY(!QFileInfo::exists(part));
        QCOMPARE(stateOf(QStringLiteral("b")), State::Waiting);
        // Nothing is left going: the next demand starts afresh.
        QSignalSpy ready(preloader_.get(), &Playback::Preloader::ready);
        preloader_->demand(upcoming(QStringLiteral("c")));
        QTRY_COMPARE(ready.size(), 1);
    }

private:
    std::unique_ptr<QTemporaryDir> dir_;
    std::unique_ptr<FakeUpstream> upstream_;
    std::unique_ptr<Playback::TrackCache> cache_;
    std::unique_ptr<Playback::Preloader> preloader_;
    QStringList resolveCalls_;
    QSet<QString> unresolvable_;
    QHash<QString, QString> freshPaths_;
};

QObject* makePreloaderTest() { return new PreloaderTest; }

} // namespace Tests

#include "PreloaderTest.moc"
