#include <QAbstractItemModel>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>

#include <QClipboard>

#include "AuraImageProvider.h"
#include "Backend.h"
#include "CoverImageProvider.h"
#include "FakeBackend.h"
#include "Host.h"
#include "LogModel.h"
#include "MobileCore.h"
#include "Platform.h"
#include "QrImageProvider.h"
#include "TestSupport.h"

#include <QtQml/QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(Cloudmus_MobilePlugin)

namespace {

void writeLog(const QString& path, const QByteArray& text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::Append));
    file.write(text);
}

Playlist fakePlaylist()
{
    Playlist playlist;
    playlist.id = QStringLiteral("p1");
    playlist.title = QStringLiteral("Playlist One");
    playlist.kind = QStringLiteral("playlist");
    return playlist;
}

// Starts p1 of the fake source once it's up.
void playFake(App::MobileCore& core)
{
    QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(QStringLiteral("fake")) != nullptr
            && core.core().sourceManager().client(QStringLiteral("fake"))->available(),
        10000);
    Tests::await(core.activePlaylist().activateAndPlay(QStringLiteral("fake"), fakePlaylist()));
}

int roleOf(const QAbstractItemModel& model, const QByteArray& name) { return model.roleNames().key(name, -1); }

} // namespace

// A slot for a signal of a replica, which has no member function to point at.
class Receiver : public QObject {
    Q_OBJECT
public slots:
    void run() { emit ran(); }
signals:
    void ran();
};

// App::Core behind the views, over the fake backend of the desktop's tests.
class SharedCoreTest : public QObject {
    Q_OBJECT

    QTemporaryDir settingsDir_;

private slots:
    void qrCodeIsSharpBlackAndWhite()
    {
        Ui::QrImageProvider provider;
        QSize size;
        const QImage image = provider.requestImage(
            QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("https://ya.ru/device"))), &size, QSize(240, 240));
        QCOMPARE(size, image.size());
        QVERIFY(size.width() > 0 && size.width() <= 240);
        QCOMPARE(size.width(), size.height());
        QSet<QRgb> colors;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x)
                colors.insert(image.pixel(x, y));
        }
        QCOMPARE(colors, QSet<QRgb>({ qRgb(0, 0, 0), qRgb(255, 255, 255) }));
        // The blank margin a scanner needs.
        QCOMPARE(image.pixel(0, 0), qRgb(255, 255, 255));
    }

    void initTestCase()
    {
        QVERIFY(settingsDir_.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        Tests::installFakeBackendManifest();
    }

    // What plays reaches the views in another process: the rows, the current
    // one, and the track's own properties, each after every change.
    void replicasFollowTheCore()
    {
        QTemporaryDir dir;
        App::MobileCore core({ QString(), dir.filePath("core.ini") });
        Remote::Host host(core, dir.filePath("core.sock"));
        QVERIFY(host.listening());
        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);

        playFake(core);

        QObject* nowPlaying = backend.nowPlaying();
        QObject* playlist = backend.playlist();
        QAbstractItemModel* tracks = backend.tracks();
        QTRY_COMPARE(playlist->property("title").toString(), QStringLiteral("Playlist One"));
        QTRY_COMPARE(tracks->rowCount(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(core.nowPlaying().hasTrack(), 10000);
        QTRY_COMPARE(nowPlaying->property("title").toString(), core.nowPlaying().title());
        QVERIFY(!nowPlaying->property("title").toString().isEmpty());
        const QString first = nowPlaying->property("title").toString();
        QTRY_COMPARE(tracks->data(tracks->index(0, 0), roleOf(*tracks, "isCurrent")).toBool(), true);

        // A slot called on the replica runs in the core.
        QMetaObject::invokeMethod(playlist, "playRow", Q_ARG(int, 1));
        QTRY_COMPARE(core.core().playback().currentIndex(), 1);
        QTRY_VERIFY(nowPlaying->property("title").toString() != first);
        QTRY_COMPARE(nowPlaying->property("title").toString(), core.nowPlaying().title());
        QTRY_COMPARE(tracks->data(tracks->index(1, 0), roleOf(*tracks, "isCurrent")).toBool(), true);
        QTRY_COMPARE(tracks->data(tracks->index(0, 0), roleOf(*tracks, "isCurrent")).toBool(), false);

        QMetaObject::invokeMethod(backend.settings(), "setAutoplay", Q_ARG(bool, false));
        QTRY_COMPARE(core.settings().autoplay(), false);
    }

    // The Library through a replica: sources, tiles, the star, and a tile
    // that starts playing, which says so; the active one isn't started over.
    void libraryThroughAReplica()
    {
        QTemporaryDir dir;
        App::MobileCore core({ QString(), dir.filePath("core.ini") });
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);

        QObject* library = backend.library();
        QAbstractItemModel* sources = backend.sources();
        QAbstractItemModel* favorites = backend.favorites();
        QAbstractItemModel* others = backend.otherPlaylists();
        QTRY_COMPARE_WITH_TIMEOUT(sources->rowCount(), 1, 10000);
        QTRY_COMPARE(sources->data(sources->index(0, 0), roleOf(*sources, "name")).toString(),
            QStringLiteral("Fake Source"));
        QTRY_COMPARE(library->property("currentSourceId").toString(), QStringLiteral("fake"));
        QTRY_COMPARE_WITH_TIMEOUT(others->rowCount() + favorites->rowCount(), 2, 10000);

        const int before = core.library().favoriteRows().rowCount();
        QMetaObject::invokeMethod(library, "toggleFavorite", Q_ARG(QString, QStringLiteral("p2")));
        // In the core first, then through the replica.
        QTRY_VERIFY(core.library().favoriteRows().rowCount() != before);
        QTRY_COMPARE(favorites->rowCount(), core.library().favoriteRows().rowCount());

        Receiver activated;
        QVERIFY(QObject::connect(library, SIGNAL(activated()), &activated, SLOT(run())));
        QSignalSpy spy(&activated, &Receiver::ran);
        QMetaObject::invokeMethod(library, "playPlaylist", Q_ARG(QString, QStringLiteral("p1")));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
        QTRY_VERIFY(core.nowPlaying().hasTrack());
        QTRY_COMPARE(backend.tracks()->rowCount(), 2);
        const QString track = core.nowPlaying().title();

        QMetaObject::invokeMethod(backend.playlist(), "playRow", Q_ARG(int, 1));
        QTRY_COMPARE(core.core().playback().currentIndex(), 1);
        QMetaObject::invokeMethod(library, "playPlaylist", Q_ARG(QString, QStringLiteral("p1")));
        QTRY_COMPARE(spy.count(), 2);
        QCOMPARE(core.core().playback().currentIndex(), 1);
        QVERIFY(core.nowPlaying().title() != track);
    }

    // The same seed gives the same picture, another seed another, opaque
    // edge to edge, at the size asked for.
    void auraDependsOnTheSeed()
    {
        Ui::AuraImageProvider provider;
        QSize size;
        const QImage first = provider.requestImage(QStringLiteral("sLiked"), &size, QSize(320, 180));
        QCOMPARE(size, QSize(320, 180));
        QCOMPARE(provider.requestImage(QStringLiteral("sLiked"), nullptr, QSize(320, 180)), first);
        QVERIFY(provider.requestImage(QStringLiteral("s%D0%92%D0%BE%D0%BB%D0%BD%D0%B0"), nullptr, QSize(320, 180)) != first);
        for (const QPoint corner : { QPoint(0, 0), QPoint(319, 0), QPoint(0, 179), QPoint(319, 179) })
            QCOMPARE(qAlpha(first.pixel(corner)), 255);
    }

    // A cover: the note until the picture is in, drawn at the size shown; a
    // change of size stretches what's there, and only once it's over is a new
    // picture asked for, with the old one on screen until that's in.
    void coverSwapsWithoutBlinking()
    {
        QTemporaryDir dir;
        QImage red(64, 64, QImage::Format_RGB32);
        red.fill(Qt::red);
        QVERIFY(red.save(dir.filePath("red.png")));
        QImage blue(64, 64, QImage::Format_RGB32);
        blue.fill(Qt::blue);
        QVERIFY(blue.save(dir.filePath("blue.png")));

        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("cover"), new Ui::CoverImageProvider);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            Window {
                width: 400; height: 400; visible: true
                Cover { objectName: "cover"; width: 100; height: width }
            }
        )", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* cover = window->findChild<QQuickItem*>(QStringLiteral("cover"));
        QVERIFY(cover);
        QVERIFY(!cover->property("_showing").toBool());

        const QString redUrl = QUrl::fromLocalFile(dir.filePath("red.png")).toString();
        const QString blueUrl = QUrl::fromLocalFile(dir.filePath("blue.png")).toString();
        const auto shown = [&] { return QUrl::fromPercentEncoding(cover->property("shownSource").toString().toUtf8()); };

        cover->setProperty("source", redUrl);
        QTRY_VERIFY(shown().endsWith(redUrl));
        QTRY_VERIFY(cover->property("_showing").toBool());
        const QString first = shown();
        // Drawn at the size it's shown at, the radius for that.
        QCOMPARE(cover->property("_side").toInt(), 100);

        // Resizing: nothing new is asked for along the way.
        for (int width = 110; width <= 200; width += 10) {
            cover->setProperty("width", width);
            QTest::qWait(30);
            QCOMPARE(cover->property("_side").toInt(), 100);
            QVERIFY(cover->property("_showing").toBool());
        }
        QTRY_COMPARE(cover->property("_side").toInt(), 200);
        // Same picture, drawn anew at the new size, and it never left the screen.
        QTRY_COMPARE(cover->property("shownSide").toInt(), 200);
        for (int i = 0; i < 30; ++i) {
            QVERIFY(cover->property("_showing").toBool());
            QTest::qWait(20);
        }
        QCOMPARE(shown(), first);

        // Back to a size it has had, which the cache has at hand: it stays.
        cover->setProperty("width", 100);
        QTRY_COMPARE(cover->property("_side").toInt(), 100);
        QTRY_COMPARE(cover->property("shownSide").toInt(), 100);
        for (int i = 0; i < 30; ++i) {
            QVERIFY(cover->property("_showing").toBool());
            QTest::qWait(20);
        }
        cover->setProperty("width", 200);
        QTRY_COMPARE(cover->property("_side").toInt(), 200);
        for (int i = 0; i < 30; ++i) {
            QVERIFY(cover->property("_showing").toBool());
            QTest::qWait(20);
        }

        // Another cover: the old one stays until the new one has come.
        cover->setProperty("source", blueUrl);
        QVERIFY(cover->property("_showing").toBool());
        QTRY_VERIFY(shown().endsWith(blueUrl));
        QVERIFY(cover->property("_showing").toBool());
    }

    // The hero's cover changes as on the desktop: the new one settling in from
    // larger, the old one on its way out; a new size of the same one just fades.
    void heroCoverZoomsOnChangeOnly()
    {
        QTemporaryDir dir;
        for (const char* name : { "red", "blue" }) {
            QImage image(64, 64, QImage::Format_RGB32);
            image.fill(QColor(QString::fromLatin1(name)));
            QVERIFY(image.save(dir.filePath(QStringLiteral("%1.png").arg(QString::fromLatin1(name)))));
        }
        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("cover"), new Ui::CoverImageProvider);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            Window {
                width: 400; height: 400; visible: true
                Cover { objectName: "cover"; width: 100; height: width; zoomOnChange: true }
            }
        )", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* cover = window->findChild<QQuickItem*>(QStringLiteral("cover"));
        QVERIFY(cover);
        const auto url = [&](const char* name) {
            return QUrl::fromLocalFile(dir.filePath(QStringLiteral("%1.png").arg(QString::fromLatin1(name)))).toString();
        };

        cover->setProperty("source", url("red"));
        QTRY_VERIFY(cover->property("_showing").toBool());
        QTRY_VERIFY(!cover->property("changing").toBool());

        // Another cover: it comes in larger, and settles.
        cover->setProperty("source", url("blue"));
        QTRY_VERIFY(cover->property("changing").toBool());
        QVERIFY(cover->property("enteringScale").toReal() > 1.0);
        QVERIFY(cover->property("_showing").toBool());
        QTRY_VERIFY(!cover->property("changing").toBool());
        QCOMPARE(cover->property("enteringScale").toReal(), 1.0);

        // The same cover at a new size: no zoom along the way.
        cover->setProperty("width", 180);
        QTRY_VERIFY_WITH_TIMEOUT(cover->property("changing").toBool(), 2000);
        while (cover->property("changing").toBool()) {
            QCOMPARE(cover->property("enteringScale").toReal(), 1.0);
            QTest::qWait(10);
        }
        QVERIFY(cover->property("_showing").toBool());
        QCOMPARE(cover->property("shownSide").toInt(), 180);
    }

    // However little a line has moved, the edge it has moved past shows no cut:
    // right at the edge there's nothing of it, and the fade is as wide as the move.
    void marqueeEdgeNeverShowsACut()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            Window {
                width: 240; height: 80; visible: true; color: "black"
                MarqueeText {
                    objectName: "line"; x: 20; y: 20; width: 200; color: "white"; font.pixelSize: 28
                    text: "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"
                }
            }
        )", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* line = window->findChild<QQuickItem*>(QStringLiteral("line"));
        QVERIFY(line);
        const qreal max = line->property("maxOffset").toReal();
        QVERIFY(max > 100);

        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        for (const qreal offset : { 0.0, 4.0, 60.0 }) {
            line->setProperty("phase", offset / max);
            QTest::qWait(100);
            QCOMPARE(line->property("leftFadeWidth").toReal(), qMin(offset, line->property("_fade").toReal()));
            const QImage image = window->grabWindow();
            if (!shots.isEmpty()) {
                QDir().mkpath(shots);
                image.scaled(image.size() * 3, Qt::KeepAspectRatio, Qt::FastTransformation)
                    .save(QStringLiteral("%1/edge-%2.png").arg(shots).arg(int(offset)));
            }
            // Where a mask can do it: the last column before the edge has nothing
            // of the line that has moved past it.
            if (offset > 0 && window->rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
                int brightest = 0;
                for (int y = 0; y < image.height(); ++y)
                    brightest = qMax(brightest, qGray(image.pixel(qRound(line->x() * image.devicePixelRatio()), y)));
                QVERIFY2(brightest < 64, qPrintable(QStringLiteral("%1 at the edge").arg(brightest)));
                // ...and well inside the fade, some of it is there: it fades, it
                // isn't cut further in.
                const qreal fade = line->property("leftFadeWidth").toReal();
                if (fade >= 20) {
                    const int inside = qRound((line->x() + fade * 0.85) * image.devicePixelRatio());
                    int seen = 0;
                    for (int x = inside - 3; x <= inside + 3; ++x) {
                        for (int y = 0; y < image.height(); ++y)
                            seen = qMax(seen, qGray(image.pixel(x, y)));
                    }
                    QVERIFY2(seen > 100, qPrintable(QStringLiteral("%1 inside the fade").arg(seen)));
                }
            }
        }
    }

    // Lines that don't fit go back and forth as one, however different their
    // lengths: at the same point of the way all the time, there and back; each
    // dissolving only at an edge with more cut off there. One that fits stays put.
    void longLinesMoveTogether()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            Window {
                width: 300; height: 120; visible: true; color: "midnightblue"
                TrackTitleBlock {
                    objectName: "block"; x: 20; y: 30; width: 200
                    titleColor: "white"; artistColor: "white"; pauseMs: 100; speed: 400; duration: 50
                    title: "Short"; artist: "Short too"
                }
            }
        )", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* block = window->findChild<QQuickItem*>(QStringLiteral("block"));
        QVERIFY(block);
        // Children by the visual tree's way of finding: they're in a layer's item.
        const auto lineNamed = [&](const QString& name) {
            QQuickItem* found = nullptr;
            const std::function<void(QQuickItem*)> look = [&](QQuickItem* item) {
                if (item->objectName() == name)
                    found = item;
                for (QQuickItem* child : item->childItems())
                    look(child);
            };
            look(block);
            return found;
        };
        auto* title = lineNamed(QStringLiteral("incomingTitle"));
        auto* artist = lineNamed(QStringLiteral("incomingArtist"));
        QVERIFY(title && artist);
        const auto real = [](QQuickItem* item, const char* name) { return item->property(name).toReal(); };

        QVERIFY(!title->property("overflowing").toBool());
        QTest::qWait(300);
        QCOMPARE(block->property("scrollPhase").toReal(), 0.0);
        QCOMPARE(real(title, "leftFade"), 0.0);
        QCOMPARE(real(title, "rightFade"), 0.0);

        // Lengths far apart.
        block->setProperty("title", QStringLiteral("A very long title that certainly does not fit here at all, by a good way"));
        block->setProperty("artist", QStringLiteral("An artist whose name is rather too long to fit in here"));
        QTRY_VERIFY(title->property("overflowing").toBool());
        QTRY_VERIFY(artist->property("overflowing").toBool());
        QVERIFY(real(title, "maxOffset") > real(artist, "maxOffset") * 1.5);
        // At the start: only the right edges are cut off.
        QCOMPARE(real(title, "leftFade"), 0.0);
        QCOMPARE(real(title, "rightFade"), 1.0);

        // A little way along: already dissolving right down to nothing at the
        // edge, over as much as has gone past it.
        QTRY_VERIFY(real(title, "_offset") > 0);
        {
            const qreal offset = real(title, "_offset");
            QCOMPARE(real(title, "leftFadeWidth"), qMin(real(title, "_fade"), offset));
        }

        // Several rounds: never apart, with both reaching both ends together.
        int ends = 0, starts = 0;
        qreal last = 0;
        QElapsedTimer clock;
        clock.start();
        while ((ends < 3 || starts < 3) && clock.elapsed() < 20000) {
            const qreal phaseTitle = real(title, "_offset") / real(title, "maxOffset");
            const qreal phaseArtist = real(artist, "_offset") / real(artist, "maxOffset");
            QVERIFY2(qAbs(phaseTitle - phaseArtist) < 0.001,
                qPrintable(QStringLiteral("%1 and %2").arg(phaseTitle).arg(phaseArtist)));
            const qreal phase = block->property("scrollPhase").toReal();
            if (phase == 1.0 && last != 1.0) {
                ++ends;
                QCOMPARE(real(title, "rightFade"), 0.0);
                QCOMPARE(real(artist, "rightFade"), 0.0);
                QCOMPARE(real(title, "leftFade"), 1.0);
                QCOMPARE(real(artist, "leftFade"), 1.0);
            }
            if (phase == 0.0 && last != 0.0)
                ++starts;
            last = phase;
            QTest::qWait(5);
        }
        QVERIFY(ends >= 3 && starts >= 3);

        // Another track starts from the beginning; lines that fit stop it.
        block->setProperty("title", QStringLiteral("Another very long title that does not fit in here either, not at all"));
        QTRY_COMPARE(title->property("text").toString(), QStringLiteral("Another very long title that does not fit in here either, not at all"));
        block->setProperty("title", QStringLiteral("Short"));
        block->setProperty("artist", QStringLiteral("Short too"));
        QTRY_VERIFY(!title->property("overflowing").toBool());
        QTRY_COMPARE(block->property("scrollPhase").toReal(), 0.0);
    }

    // The title and artist as a block: nothing to wipe to the first time, then one
    // wipe for the two together, ending in the new text; a change along the way
    // goes straight on to the latest. The new text doesn't show before the front.
    void titleBlockWipes()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            Window {
                width: 500; height: 120; visible: true; color: "midnightblue"
                TrackTitleBlock {
                    objectName: "block"; x: 20; y: 30; width: 440
                    titleColor: "white"; artistColor: "white"
                    title: "First title"; artist: "First artist"
                }
            }
        )", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* block = window->findChild<QQuickItem*>(QStringLiteral("block"));
        QVERIFY(block);
        QCOMPARE(block->property("currentTitle").toString(), QStringLiteral("First title"));
        QVERIFY(!block->property("wiping").toBool());
        // Long or short, the width is the layout's to give.
        QCOMPARE(block->implicitWidth(), 0.0);
        QTest::qWait(100);
        const QImage before = window->grabWindow();

        // The two change one after the other: one wipe, and at its first frame
        // the screen is still as it was.
        QSignalSpy wipingChanged(block, SIGNAL(wipingChanged()));
        block->setProperty("title", QStringLiteral("Second title"));
        block->setProperty("artist", QStringLiteral("Second artist"));
        QVERIFY(wipingChanged.wait(1000));
        const QImage first = window->grabWindow();
        if (const QString dir = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS"); !dir.isEmpty()) {
            before.save(dir + QStringLiteral("/wipe-before.png"));
            first.save(dir + QStringLiteral("/wipe-first.png"));
        }
        int differ = 0;
        for (int y = 0; y < before.height(); ++y) {
            for (int x = 0; x < before.width(); ++x) {
                const QRgb a = before.pixel(x, y);
                const QRgb b = first.pixel(x, y);
                if (qAbs(qGray(a) - qGray(b)) > 48)
                    ++differ;
            }
        }
        QVERIFY2(differ < 20, qPrintable(QStringLiteral("%1 pixels changed").arg(differ)));
        QCOMPARE(block->property("currentTitle").toString(), QStringLiteral("Second title"));
        QCOMPARE(block->property("currentArtist").toString(), QStringLiteral("Second artist"));
        QTRY_VERIFY(!block->property("wiping").toBool());
        QCOMPARE(wipingChanged.count(), 2); // on and off: one wipe

        // A frame halfway, each way, to look at: the mask needs a GPU to show.
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        if (!shots.isEmpty()) {
            QDir().mkpath(shots);
            block->setProperty("duration", 4000);
            for (const int direction : { 1, -1 }) {
                block->setProperty("direction", direction);
                block->setProperty("title", direction > 0 ? QStringLiteral("Forward going title") : QStringLiteral("Backward going title"));
                block->setProperty("artist", direction > 0 ? QStringLiteral("Forward artist") : QStringLiteral("Backward artist"));
                QTest::qWait(1600);
                window->grabWindow().save(QStringLiteral("%1/wipe-block-%2.png").arg(shots).arg(direction));
                QTRY_VERIFY_WITH_TIMEOUT(!block->property("wiping").toBool(), 6000);
            }
            block->setProperty("duration", 600);
            block->setProperty("direction", 1);
        }

        // A change along the way ends in the latest.
        block->setProperty("title", QStringLiteral("Third"));
        QTRY_VERIFY(block->property("wiping").toBool());
        block->setProperty("title", QStringLiteral("Fourth"));
        QTRY_COMPARE(block->property("currentTitle").toString(), QStringLiteral("Fourth"));
        QTRY_VERIFY(!block->property("wiping").toBool());

        // The same text again: nothing.
        block->setProperty("title", QStringLiteral("Fourth"));
        QTest::qWait(80);
        QVERIFY(!block->property("wiping").toBool());
    }

    // The hero's: its edges dissolve into the color given, its center is dark.
    void auraHeroHasEdgesAndAVignette()
    {
        Ui::AuraImageProvider provider;
        const QSize size(400, 400);
        const QImage plain = provider.requestImage(QStringLiteral("sLiked"), nullptr, size);
        const QImage hero = provider.requestImage(QStringLiteral("h102030/40/Liked"), nullptr, size);
        QCOMPARE(hero.size(), size);
        const auto brightness = [](const QImage& image, const QPoint& at) {
            return qGray(image.pixel(at));
        };
        QVERIFY(brightness(hero, QPoint(200, 200)) < brightness(plain, QPoint(200, 200)));
        // At the very edge about half of the fade's color, in the corner most of it.
        const QColor corner = hero.pixelColor(0, 0);
        QVERIFY(qAbs(corner.green() - 0x20) < 24);
        QCOMPARE(qAlpha(hero.pixel(200, 200)), 255);
    }

    // Rounded corners in the image's own transparency, for a real file and for
    // generated art, cut square whatever shape the file is.
    void coversHaveRoundedCorners()
    {
        Ui::CoverImageProvider provider;
        QTemporaryDir dir;
        QImage wide(200, 100, QImage::Format_RGB32);
        wide.fill(Qt::red);
        QVERIFY(wide.save(dir.filePath("wide.png")));
        const QString file = QString::fromLatin1(QUrl::toPercentEncoding(QUrl::fromLocalFile(dir.filePath("wide.png")).toString()));
        for (const QString& id : { QStringLiteral("12/f") + file, QStringLiteral("12/sLiked") }) {
            QSize size;
            const QImage image = provider.requestImage(id, &size, QSize(64, 64));
            QCOMPARE(size, QSize(64, 64));
            for (const QPoint corner : { QPoint(0, 0), QPoint(63, 0), QPoint(0, 63), QPoint(63, 63) })
                QCOMPARE(qAlpha(image.pixel(corner)), 0);
            QCOMPARE(qAlpha(image.pixel(32, 32)), 255);
            QCOMPARE(qAlpha(image.pixel(32, 0)), 255);
        }
        QVERIFY(provider.requestImage(QStringLiteral("12/ffile%3A%2F%2F%2Fnone.png"), nullptr, QSize(64, 64)).isNull());
    }

    // What the source says and what the user does reach the row's roles, and
    // through a replica: the fake's liked list comes with `liked` set.
    void likesReachTheRows()
    {
        QTemporaryDir dir;
        App::MobileCore core({ QString(), dir.filePath("core.ini") });
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);
        playFake(core);
        QAbstractItemModel* tracks = backend.tracks();
        QTRY_COMPARE(tracks->rowCount(), 2);
        const auto role = [&](int row, const QByteArray& name) {
            return tracks->data(tracks->index(row, 0), roleOf(*tracks, name));
        };
        Tests::await(core.nowPlaying().setTrackLiked(
            QStringLiteral("fake"), QStringLiteral("t2"), true, /*announce=*/false));
        QTRY_COMPARE(role(1, "liked").toBool(), true);
        QVERIFY(!role(0, "liked").toBool());
        Tests::await(core.nowPlaying().setTrackDisliked(
            QStringLiteral("fake"), QStringLiteral("t1"), true, /*announce=*/false));
        QTRY_COMPARE(role(0, "disliked").toBool(), true);
        // Undone, for what the other tests start from.
        Tests::await(core.nowPlaying().setTrackLiked(
            QStringLiteral("fake"), QStringLiteral("t2"), false, /*announce=*/false));
        Tests::await(core.nowPlaying().setTrackDisliked(
            QStringLiteral("fake"), QStringLiteral("t1"), false, /*announce=*/false));
    }

    // A playlist opened to look at, through a replica: its rows, the filter,
    // playing a filtered row plays the right track and makes it active; the
    // sheet picks a row, tells of it, and lists the user's playlists.
    void playlistAndTrackScreens()
    {
        QTemporaryDir dir;
        App::MobileCore core({ QString(), dir.filePath("core.ini") });
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(QStringLiteral("fake")) != nullptr
                && core.core().sources().playlists(QStringLiteral("fake")).size() == 2,
            10000);

        QObject* screen = backend.playlistScreen();
        QAbstractItemModel* rows = backend.openTracks();
        QMetaObject::invokeMethod(screen, "openPlaylist", Q_ARG(QString, QStringLiteral("fake")),
            Q_ARG(QString, QStringLiteral("p1")));
        QTRY_COMPARE(screen->property("title").toString(), QStringLiteral("First"));
        QTRY_COMPARE(rows->rowCount(), 2);

        QMetaObject::invokeMethod(screen, "setFilter", Q_ARG(QString, QStringLiteral("t2")));
        QTRY_COMPARE(rows->rowCount(), 1);
        QTRY_COMPARE(rows->data(rows->index(0, 0), roleOf(*rows, "trackId")).toString(), QStringLiteral("t2"));

        QObject* track = backend.trackScreen();
        QMetaObject::invokeMethod(track, "showOpenRow", Q_ARG(int, 0));
        QTRY_COMPARE(track->property("title").toString(), QStringLiteral("Track t2"));
        QTRY_COMPARE(track->property("sourceName").toString(), QStringLiteral("Fake Source"));
        QTRY_COMPARE(track->property("canPlay").toBool(), true);
        QMetaObject::invokeMethod(track, "loadPlaylists");
        QTRY_VERIFY(backend.trackPlaylists()->rowCount() > 0);

        QMetaObject::invokeMethod(screen, "playRow", Q_ARG(int, 0));
        QTRY_VERIFY(core.nowPlaying().hasTrack());
        QTRY_COMPARE(core.nowPlaying().track().id, QStringLiteral("t2"));
        QTRY_COMPARE(core.activePlaylist().context().playlist.id, QStringLiteral("p1"));
    }

    // A list's items dissolve at an edge only with more beyond it, and that
    // comes and goes with scrolling; a list that fits has none.
    void listsDissolveAtEdgesWithMoreBeyond()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            Window {
                width: 300; height: 400; visible: true; color: Theme.surface0
                ScrollList {
                    objectName: "long"
                    x: 0; width: 150; height: 400
                    model: 50
                    delegate: Rectangle { width: 150; height: 40; color: "red" }
                }
                ScrollList {
                    objectName: "short"
                    x: 150; width: 150; height: 400
                    model: 2
                    delegate: Rectangle { width: 150; height: 40; color: "red" }
                }
            }
        )", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto opacity = [&](const char* list, const char* edge) {
            auto* view = window->findChild<QQuickItem*>(QString::fromLatin1(list));
            const QList<QQuickItem*> found = view->findChildren<QQuickItem*>(QString::fromLatin1(edge));
            return found.isEmpty() ? -1.0 : found.first()->opacity();
        };
        QTRY_COMPARE(opacity("long", "fadeBottom"), 1.0);
        QTRY_COMPARE(opacity("long", "fadeTop"), 0.0);
        QTRY_COMPARE(opacity("short", "fadeBottom"), 0.0);
        QTRY_COMPARE(opacity("short", "fadeTop"), 0.0);

        auto* view = window->findChild<QQuickItem*>(QStringLiteral("long"));
        view->setProperty("contentY", 200);
        QTRY_COMPARE(opacity("long", "fadeTop"), 1.0);
        QCOMPARE(opacity("long", "fadeBottom"), 1.0);
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        if (!shots.isEmpty())
            window->grabWindow().save(shots + QStringLiteral("/edge-fades.png"));

        // Scrolled to the very end: nothing below any more.
        QMetaObject::invokeMethod(view, "positionViewAtEnd");
        QTRY_COMPARE(opacity("long", "fadeBottom"), 0.0);
        QCOMPARE(opacity("long", "fadeTop"), 1.0);
    }

    // Messages reach the views, in plain types; the Downloads screen through a
    // replica: off until asked for, a job's row while it's under way, cancelling it.
    void messagesAndDownloads()
    {
        QTemporaryDir dir;
        App::MobileCore core({ QString(), dir.filePath("core.ini") });
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);

        QSignalSpy messages(backend.mainScreen(), SIGNAL(message(int,QString)));
        QVERIFY(messages.isValid());
        core.core().messages().error(QStringLiteral("It broke"));
        // The fake source may have a word of its own in ahead of it.
        const auto ourMessage = [&messages]() {
            return std::find_if(messages.cbegin(), messages.cend(),
                [](const QList<QVariant>& message) { return message.at(1).toString() == QStringLiteral("It broke"); });
        };
        QTRY_VERIFY(ourMessage() != messages.cend());
        QCOMPARE(ourMessage()->at(0).toInt(), int(ViewModel::Messages::Kind::Error));

        QObject* screen = backend.downloadsScreen();
        QAbstractItemModel* rows = backend.downloads();
        QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(QStringLiteral("fake")) != nullptr
                && core.core().sourceManager().client(QStringLiteral("fake"))->available(),
            10000);
        QMetaObject::invokeMethod(screen, "setEnabled", Q_ARG(bool, false));
        QTRY_COMPARE(screen->property("enabled").toBool(), false);
        QMetaObject::invokeMethod(screen, "setEnabled", Q_ARG(bool, true));
        QTRY_COMPARE(screen->property("enabled").toBool(), true);
        QVERIFY(!screen->property("directory").toString().isEmpty());

        // Never ends by itself, in the fake: see FakeBackend.
        Track slow;
        slow.id = QStringLiteral("slow");
        slow.title = QStringLiteral("Slow one");
        slow.durationMs = 1000;
        core.core().downloads().downloadTrack(QStringLiteral("fake"), slow);
        QTRY_COMPARE(rows->rowCount(), 1);
        QTRY_COMPARE(rows->data(rows->index(0, 0), roleOf(*rows, "title")).toString(), QStringLiteral("Slow one"));
        // (A replica's row is empty until fetched: wait for the real id.)
        QTRY_VERIFY(rows->data(rows->index(0, 0), roleOf(*rows, "jobId")).toInt() > 0);
        QCOMPARE(rows->data(rows->index(0, 0), roleOf(*rows, "isOver")).toBool(), false);
        const int jobId = rows->data(rows->index(0, 0), roleOf(*rows, "jobId")).toInt();
        QMetaObject::invokeMethod(screen, "cancel", Q_ARG(int, jobId));
        QTRY_COMPARE(rows->data(rows->index(0, 0), roleOf(*rows, "isOver")).toBool(), true);
        QTRY_COMPARE(screen->property("canClear").toBool(), true);
        QMetaObject::invokeMethod(screen, "clearFinished");
        QTRY_COMPARE(rows->rowCount(), 0);
        QMetaObject::invokeMethod(screen, "setEnabled", Q_ARG(bool, false));
        QTRY_COMPARE(core.core().downloads().isEnabled(), false);
    }

    // The main list's filter, through a replica: a filtered row plays and shows
    // the right track. And the queue's modes from the buttons' slots.
    void mainFilterAndPlayModes()
    {
        QTemporaryDir dir;
        App::MobileCore core({ QString(), dir.filePath("core.ini") });
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);
        playFake(core);
        QAbstractItemModel* tracks = backend.tracks();
        QTRY_COMPARE(tracks->rowCount(), 2);

        QMetaObject::invokeMethod(backend.mainScreen(), "setFilter", Q_ARG(QString, QStringLiteral("t2")));
        QTRY_COMPARE(tracks->rowCount(), 1);
        QTRY_COMPARE(tracks->data(tracks->index(0, 0), roleOf(*tracks, "trackId")).toString(), QStringLiteral("t2"));

        QObject* track = backend.trackScreen();
        QMetaObject::invokeMethod(track, "showActiveRow", Q_ARG(int, 0));
        QTRY_COMPARE(track->property("title").toString(), QStringLiteral("Track t2"));

        QMetaObject::invokeMethod(backend.mainScreen(), "playRow", Q_ARG(int, 0));
        QTRY_COMPARE(core.core().playback().currentIndex(), 1);
        QTRY_COMPARE(core.nowPlaying().track().id, QStringLiteral("t2"));

        QMetaObject::invokeMethod(backend.mainScreen(), "setFilter", Q_ARG(QString, QString()));
        QTRY_COMPARE(tracks->rowCount(), 2);

        QObject* now = backend.nowPlaying();
        const bool shuffle = core.nowPlaying().shuffle();
        QMetaObject::invokeMethod(now, "toggleShuffle");
        QTRY_COMPARE(core.nowPlaying().shuffle(), !shuffle);
        QTRY_COMPARE(now->property("shuffle").toBool(), !shuffle);
        QMetaObject::invokeMethod(now, "toggleShuffle");
        QTRY_COMPARE(core.nowPlaying().shuffle(), shuffle);

        const int repeat = core.nowPlaying().repeat();
        QMetaObject::invokeMethod(now, "cycleRepeat");
        QTRY_VERIFY(core.nowPlaying().repeat() != repeat);
        QTRY_COMPARE(now->property("repeat").toInt(), core.nowPlaying().repeat());
        core.nowPlaying().setRepeatMode(Playback::RepeatMode(repeat));
    }

    // A row tells a track on disk and a download under way, not otherwise.
    void trackRowShowsTheCache()
    {
        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("aura"), new Ui::AuraImageProvider);
        engine.addImageProvider(QStringLiteral("cover"), new Ui::CoverImageProvider);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            Window {
                width: 420; height: 300; visible: true; color: Theme.surface0
                ListView {
                    anchors.fill: parent
                    model: ListModel {
                        ListElement { title: "Cached"; artist: "A"; durationMs: 1000; coverUrl: ""; isCurrent: false
                                      cached: true; downloadProgress: -1; liked: true; disliked: false; unavailable: false; playedAt: "" }
                        ListElement { title: "Fetching"; artist: "A"; durationMs: 1000; coverUrl: ""; isCurrent: false
                                      cached: false; downloadProgress: 0.4; liked: false; disliked: true; unavailable: false; playedAt: "" }
                        ListElement { title: "Neither"; artist: "A"; durationMs: 1000; coverUrl: ""; isCurrent: false
                                      cached: false; downloadProgress: -1; liked: false; disliked: false; unavailable: true; playedAt: "" }
                    }
                    delegate: TrackRow {}
                }
            }
        )", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(100);
        // By the visual tree: a delegate's QObject parent isn't the window.
        QList<QQuickItem*> marks;
        const std::function<void(QQuickItem*)> collect = [&](QQuickItem* item) {
            if (item->objectName() == QStringLiteral("cachedMark"))
                marks.append(item);
            for (QQuickItem* child : item->childItems())
                collect(child);
        };
        collect(window->contentItem());
        QCOMPARE(marks.size(), 3);
        const auto shown = [](const QList<QQuickItem*>& items) {
            return std::count_if(items.cbegin(), items.cend(), [](QQuickItem* item) { return item->isVisible(); });
        };
        QCOMPARE(shown(marks), 1);
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        if (!shots.isEmpty()) {
            QDir().mkpath(shots);
            window->grabWindow().save(shots + QStringLiteral("/track-row.png"));
        }
    }

    void logModelReadsAndFilters()
    {
        QTemporaryDir dir;
        writeLog(dir.filePath("playback-1.log"),
            "2026-10-31 12:00:00.100 [   10.000] playback/main I Startup: core ready\n"
            "2026-10-31 12:00:00.300 [   10.200] playback/main E Engine: failed\n"
            "    at line one\n");
        writeLog(dir.filePath("ui-1.log"), "2026-10-31 12:00:00.200 [   10.100] ui/main D qt: hello\n");

        Ui::LogModel model;
        model.setDirectory(dir.path());
        QTRY_COMPARE(model.rowCount(), 3); // read on a worker thread
        // Merged by time across the two files.
        QCOMPARE(model.data(model.index(1), Ui::LogModel::ProcessRole).toString(), QStringLiteral("ui"));
        QCOMPARE(
            model.data(model.index(2), Ui::LogModel::MessageRole).toString(), QStringLiteral("failed\nat line one"));

        model.setMinLevel("W");
        QCOMPARE(model.rowCount(), 1);
        model.setMinLevel("D");
        model.setProcess("playback");
        QCOMPARE(model.rowCount(), 2);
        model.setProcess("");
        model.setTag("Startup");
        QCOMPARE(model.rowCount(), 1);
        model.setTag("");

        // A new tail is picked up on the next poll.
        writeLog(dir.filePath("ui-1.log"), "2026-10-31 12:00:01.000 [   10.900] ui/main W qt: late\n");
        QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 4, 3000);
    }

    // The pages come up and switch over the real core and the dev tracks;
    // CLOUDMUS_MOBILE_SHOTS=<dir> also saves each, at a phone's and a head
    // unit's size, to look at.
    // No network is a state to see, not an error.
    void offlineIsShownNotComplainedAbout()
    {
        QTemporaryDir dir;
        App::MobileCore core({ QString(), dir.filePath("core.ini") });
        Ui::Backend backend(core);
        Ui::Platform platform(dir.path());
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("aura"), new Ui::AuraImageProvider);
        engine.addImageProvider(QStringLiteral("cover"), new Ui::CoverImageProvider);
        engine.setInitialProperties({
            { "backend", QVariant::fromValue<QObject*>(&backend) },
            { "platform", QVariant::fromValue<QObject*>(&platform) },
        });
        engine.loadFromModule("Cloudmus.Mobile", "Main");
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1280, 720);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* badge = window->findChild<QQuickItem*>("offlineBadge");
        QVERIFY(badge);
        core.core().connectivity().reportReached(QStringLiteral("fake"));
        QVERIFY(!badge->isVisible());

        QSignalSpy messages(&core.mainScreen(), &ViewModel::MainScreen::message);
        core.core().connectivity().reportOffline(QStringLiteral("fake"));
        core.core().messages().offline(QStringLiteral("no network"));
        QTRY_VERIFY(badge->isVisible());
        QCOMPARE(messages.size(), 0);

        core.core().connectivity().reportReached(QStringLiteral("fake"));
        QTRY_VERIFY(!badge->isVisible());
    }

    // A phone on its side has no height for a cover above the title: it goes
    // beside it, clear of the source's button and of the text.
    void aLowPanelPutsTheCoverBesideTheTitle()
    {
        QTemporaryDir dir;
        App::MobileCore core({ QString(), dir.filePath("core.ini") });
        Ui::Backend backend(core);
        Ui::Platform platform(dir.path());
        playFake(core);

        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("aura"), new Ui::AuraImageProvider);
        engine.addImageProvider(QStringLiteral("cover"), new Ui::CoverImageProvider);
        engine.setInitialProperties({
            { "backend", QVariant::fromValue<QObject*>(&backend) },
            { "platform", QVariant::fromValue<QObject*>(&platform) },
        });
        engine.loadFromModule("Cloudmus.Mobile", "Main");
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(720, 330);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(500);

        auto* panel = window->findChild<QQuickItem*>("trackPanel");
        auto* cover = window->findChild<QQuickItem*>("trackCover");
        auto* title = window->findChild<QQuickItem*>("trackTitle");
        auto* bar = window->findChild<QQuickItem*>("sourceBar");
        QVERIFY(panel && cover && title && bar);
        QVERIFY(panel->property("compact").toBool());
        const auto scene = [](QQuickItem* item) { return item->mapRectToScene(item->boundingRect()); };
        QVERIFY(cover->width() >= 60);
        QVERIFY2(!scene(cover).intersects(scene(bar)), "the cover runs into the source's button");
        QVERIFY2(!scene(cover).intersects(scene(title)), "the cover runs into the title");
        QVERIFY(scene(cover).bottom() <= scene(panel).bottom() + 1);
        // Shuffle and repeat are up in the source's row, and the title has their room.
        auto* topShuffle = window->findChild<QQuickItem*>("topShuffle");
        QVERIFY(topShuffle && topShuffle->isVisible());
        QVERIFY(!window->findChild<QQuickItem*>("shuffleButton")->isVisible());
        QVERIFY(!scene(topShuffle).intersects(scene(bar)));
        QVERIFY(scene(title).width() >= 120);
        // The count went up to the list's title.
        QVERIFY(window->findChild<QQuickItem*>("playlistCount")->isVisible());
        QTRY_COMPARE(window->findChild<QQuickItem*>("playlistCount")->property("text").toString(), QStringLiteral("2"));
        auto* details = window->findChild<QQuickItem*>("trackDetails");
        QVERIFY(details);
        QVERIFY2(details->implicitWidth() <= panel->width(),
            qPrintable(QStringLiteral("the panel's content takes %1 of %2").arg(details->implicitWidth()).arg(panel->width())));
        // It didn't get here by itself: the mode changing sizes that decide the
        // mode froze the screen. To the track's screen and back, to the end.
        auto* toggle = window->findChild<QQuickItem*>("screenModeToggle");
        QVERIFY(toggle);
        const qreal normalSide = cover->width();
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QTRY_VERIFY(cover->width() > normalSide * 1.3);
        QTRY_COMPARE(panel->property("progress").toReal(), 1.0);
        QVERIFY(window->findChild<QQuickItem*>("shuffleButton")->isVisible());
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QTRY_COMPARE(panel->property("progress").toReal(), 0.0);
        QTRY_COMPARE(cover->width(), normalSide);
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        if (!shots.isEmpty())
            window->grabWindow().save(shots + QStringLiteral("/phone-landscape.png"));
    }

    // Signing in on a narrow screen: all of it fits, the code copies, and once
    // signed in there's a way out.
    void signingInFitsAPhoneAndCanBeUndone()
    {
        qputenv("CLOUDMUS_FAKE_AUTH", "1");
        QTemporaryDir dir;
        App::MobileCore core({ QString(), dir.filePath("core.ini") });
        Ui::Backend backend(core);
        Ui::Platform platform(dir.path());
        const QString fake = QStringLiteral("fake");
        QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(fake) != nullptr
                && core.core().sourceManager().client(fake)->available(),
            10000);
        qunsetenv("CLOUDMUS_FAKE_AUTH");

        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("aura"), new Ui::AuraImageProvider);
        engine.addImageProvider(QStringLiteral("cover"), new Ui::CoverImageProvider);
        engine.addImageProvider(QStringLiteral("qr"), new Ui::QrImageProvider);
        engine.setInitialProperties({
            { "backend", QVariant::fromValue<QObject*>(&backend) },
            { "platform", QVariant::fromValue<QObject*>(&platform) },
        });
        engine.loadFromModule("Cloudmus.Mobile", "Main");
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(360, 780);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        core.library().selectSource(fake);
        core.core().authStates().setPrompt(fake,
            { { QStringLiteral("flow"), QStringLiteral("deviceCode") }, { QStringLiteral("url"), QStringLiteral("https://ya.ru/device") },
                { QStringLiteral("code"), QStringLiteral("abc123") } });
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "libraryRequested"));
        QTest::qWait(600);
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        if (!shots.isEmpty())
            window->grabWindow().save(shots + QStringLiteral("/phone-signin.png"));

        for (const char* name : { "authLink", "authCode", "copyAuthCode", "authQr" }) {
            auto* item = window->findChild<QQuickItem*>(name);
            QVERIFY2(item != nullptr, name);
            QVERIFY2(item->isVisible(), name);
            const QRectF rect = item->mapRectToScene(item->boundingRect());
            QVERIFY2(rect.left() >= 0 && rect.right() <= window->width(), name);
        }
        QVERIFY(window->findChild<QQuickItem*>("authLink")->property("text").toString().contains(
            QStringLiteral("<a href=\"https://ya.ru/device\">")));
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("copyAuthCode"), "clicked"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("abc123"));

        auto* signOut = window->findChild<QQuickItem*>("signOut");
        QVERIFY(signOut);
        QVERIFY(!signOut->isVisible());
        core.core().authStates().setAuthenticated(fake);
        QTRY_VERIFY(signOut->isVisible());
        QVERIFY(QMetaObject::invokeMethod(signOut, "clicked"));
        QTRY_VERIFY(core.library().authNeeded());
        QTRY_VERIFY(!signOut->isVisible());
    }

    void pagesOpen_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("phone") << QSize(420, 860);
        QTest::newRow("headunit") << QSize(1920, 1080);
        QTest::newRow("smallunit") << QSize(1280, 720);
    }

    void pagesOpen()
    {
        QFETCH(QSize, size);
        QTemporaryDir dir;
        App::MobileCore core({ QString(), dir.filePath("core.ini") });
        Ui::Backend backend(core);
        Ui::Platform platform(dir.path());
        playFake(core);

        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("aura"), new Ui::AuraImageProvider);
        engine.addImageProvider(QStringLiteral("cover"), new Ui::CoverImageProvider);
        engine.setInitialProperties({
            { "backend", QVariant::fromValue<QObject*>(&backend) },
            { "platform", QVariant::fromValue<QObject*>(&platform) },
        });
        engine.loadFromModule("Cloudmus.Mobile", "Main");
        QCOMPARE(engine.rootObjects().size(), 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto* stack = window->findChild<QQuickItem*>("stack");
        QVERIFY(stack);
        auto* playlist = window->findChild<QQuickItem*>("playlist");
        QVERIFY(playlist);
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        auto shot = [&](const char* name) {
            QTest::qWait(700); // transitions, async covers
            if (!shots.isEmpty()) {
                QDir().mkpath(shots);
                window->grabWindow().save(QStringLiteral("%1/%2-%3.png").arg(shots, QTest::currentDataTag(), name));
            }
        };
        shot("main");

        // The list's filter opens under its bar, and closes empty.
        playlist->setProperty("filterOpen", true);
        auto* filterField = window->findChild<QQuickItem*>("mainFilter");
        QVERIFY(filterField);
        filterField->setProperty("text", QStringLiteral("t2"));
        QTRY_COMPARE(backend.tracks()->rowCount(), 1);
        shot("main-filter");
        playlist->setProperty("filterOpen", false);
        QCOMPARE(filterField->property("text").toString(), QString());
        QTRY_COMPARE(backend.tracks()->rowCount(), 2);

        // On a head unit, the two modes of the screen: everything has moved when
        // the animation is over, the source's button clear of the cover in both,
        // and turning back restores it all.
        if (size.width() > size.height()) {
            auto* bar = window->findChild<QQuickItem*>("sourceBar");
            auto* panel = window->findChild<QQuickItem*>("trackPanel");
            auto* cover = window->findChild<QQuickItem*>("trackCover");
            auto* details = window->findChild<QQuickItem*>("trackDetails");
            auto* list = window->findChild<QQuickItem*>("mainList");
            auto* railSettings = window->findChild<QQuickItem*>("railSettings");
            auto* toggle = window->findChild<QObject*>("screenModeToggle");
            QVERIFY(bar && panel && cover && details && list && railSettings && toggle);
            const auto sceneRect = [](QQuickItem* item) {
                return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            };
            const auto settled = [&](const QByteArray& expected) {
                return playlist->property("screenProgress").toReal() == expected.toDouble();
            };
            QTRY_VERIFY(settled("0"));
            const QRectF normalCover = sceneRect(cover);
            const qreal normalList = list->width();
            const qreal normalBar = bar->width();
            const int normalTitleSize = window->findChild<QQuickItem*>("trackTitle")->property("titleSize").toInt();
            const int normalArtistSize = window->findChild<QQuickItem*>("trackTitle")->property("artistSize").toInt();
            QVERIFY(!sceneRect(bar).intersects(normalCover));
            QVERIFY(!railSettings->isVisible());
            QVERIFY(sceneRect(details).top() > normalCover.top());

            // Another track: its cover comes as on the desktop, then is there alone.
            core.activePlaylist().playRow(core.core().playback().currentIndex() == 0 ? 1 : 0);
            QTRY_VERIFY(cover->property("changing").toBool());
            QTest::qWait(120);
            if (const QString dir = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS"); !dir.isEmpty()) {
                window->grabWindow().save(
                    QStringLiteral("%1/%2-cover-changing.png").arg(dir, QTest::currentDataTag()));
                QTest::qWait(150);
                window->grabWindow().save(QStringLiteral("%1/%2-text-wiping.png").arg(dir, QTest::currentDataTag()));
            }
            QTRY_VERIFY(!cover->property("changing").toBool());
            QVERIFY(cover->property("_showing").toBool());

            QMetaObject::invokeMethod(toggle, "clicked");
            QTest::qWait(150);
            if (const QString dir = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS"); !dir.isEmpty()) {
                window->grabWindow().save(
                    QStringLiteral("%1/%2-mode-moving.png").arg(dir, QTest::currentDataTag()));
            }
            QTRY_VERIFY(settled("1"));
            // A picture of the new size comes once it has settled, and is what's shown.
            QTRY_COMPARE(cover->property("_side").toInt(), qRound(cover->width() * window->devicePixelRatio()));
            // (Nothing to show if the fake's track, whose stream can't be fetched,
            // has been given up on by now: then it's the note.)
            if (!cover->property("_wanted").toString().isEmpty())
                QTRY_VERIFY(cover->property("_showing").toBool());
            else
                qInfo() << "cover: nothing wanted, hasTrack" << core.nowPlaying().hasTrack();
            if (!cover->property("_wanted").toString().isEmpty())
                QTRY_COMPARE(cover->property("shownSide").toInt(), cover->property("_side").toInt());
            shot("mode-expanded");
            const QRectF bigCover = sceneRect(cover);
            auto* titleBlock = window->findChild<QQuickItem*>("trackTitle");
            QVERIFY(titleBlock);
            QVERIFY(titleBlock->property("titleSize").toInt() > normalTitleSize);
            QVERIFY(titleBlock->property("artistSize").toInt() > normalArtistSize);
            QVERIFY(bigCover.height() > normalCover.height());
            QVERIFY(!sceneRect(bar).intersects(bigCover));
            QCOMPARE(bar->width(), bar->height());
            QVERIFY(list->width() < normalList / 3);
            // The covers in the middle of their column.
            QQuickItem* rowCover = nullptr;
            const std::function<void(QQuickItem*)> findCover = [&](QQuickItem* item) {
                if (rowCover == nullptr && item->objectName() == QStringLiteral("rowCover"))
                    rowCover = item;
                for (QQuickItem* child : item->childItems())
                    findCover(child);
            };
            findCover(list);
            QVERIFY(rowCover);
            QVERIFY(qAbs(sceneRect(rowCover).center().x() - sceneRect(list).center().x()) <= 1.5);
            QVERIFY(sceneRect(details).left() >= bigCover.right());
            // Nothing of it pushed out of its place.
            QVERIFY(details->implicitWidth() <= details->width());
            QVERIFY(sceneRect(details).right() <= sceneRect(list).left());
            QVERIFY(railSettings->isVisible());
            QVERIFY(sceneRect(railSettings).bottom() > window->height() * 0.8);
            QVERIFY(sceneRect(railSettings).right() <= bigCover.left());

            QMetaObject::invokeMethod(toggle, "clicked");
            QTRY_VERIFY(settled("0"));
            QTRY_COMPARE(list->width(), normalList);
            QCOMPARE(bar->width(), normalBar);
            QVERIFY(!railSettings->isVisible());

            // The source's button goes to the Library, on the active playlist's source.
            QMetaObject::invokeMethod(bar, "clicked");
            QTRY_COMPARE(stack->property("depth").toInt(), 2);
            QCOMPARE(core.library().currentSourceId(), QStringLiteral("fake"));
            QMetaObject::invokeMethod(stack, "close");
            QTRY_COMPARE(stack->property("depth").toInt(), 1);
        }
        QMetaObject::invokeMethod(playlist, "libraryRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        shot("library");
        // A tile: plays, and back to the main screen.
        QMetaObject::invokeMethod(&core.library(), "playPlaylist",
            Q_ARG(QString, QStringLiteral("p2")));
        QTRY_COMPARE_WITH_TIMEOUT(stack->property("depth").toInt(), 1, 10000);
        shot("main-after-tile");
        // The mini player's title and cover change like the big panel's.
        if (size.width() <= size.height() * 1.1) {
            auto* miniTitle = window->findChild<QQuickItem*>("miniTitle");
            auto* miniCover = window->findChild<QQuickItem*>("miniCover");
            QVERIFY(miniTitle && miniCover);
            QTRY_COMPARE(miniTitle->property("currentTitle").toString(), core.nowPlaying().title());
            QTRY_VERIFY(!miniCover->property("changing").toBool());
            QTRY_VERIFY(miniCover->property("_showing").toBool());
            const QString before = core.nowPlaying().title();
            core.activePlaylist().playRow(core.core().playback().currentIndex() == 0 ? 1 : 0);
            QTRY_VERIFY(core.nowPlaying().title() != before);
            QTRY_VERIFY(miniTitle->property("wiping").toBool());
            QTRY_VERIFY(miniCover->property("changing").toBool());
            QVERIFY(miniCover->property("enteringScale").toReal() > 1.0);
            QTest::qWait(150);
            if (const QString dir = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS"); !dir.isEmpty())
                window->grabWindow().save(QStringLiteral("%1/%2-mini-changing.png").arg(dir, QTest::currentDataTag()));
            QTRY_VERIFY(!miniTitle->property("wiping").toBool());
            QTRY_COMPARE(miniTitle->property("currentTitle").toString(), core.nowPlaying().title());
            QTRY_VERIFY(!miniCover->property("changing").toBool());
        }

        // On a phone, Now Playing's background is drawn beforehand: the page has
        // it from its very first frame, rather than rising transparent.
        const bool narrow = size.width() <= size.height() * 1.1;
        if (narrow) {
            auto* preload = window->findChild<QObject*>("auraPreload");
            QVERIFY(preload);
            QTRY_COMPARE(preload->property("status").toInt(), 1); // Image.Ready
        }
        QMetaObject::invokeMethod(playlist, "nowPlayingRequested");
        if (narrow) {
            auto* aura = window->findChild<QQuickItem*>("nowPlayingAura");
            QVERIFY(aura);
            QCOMPARE(aura->property("color").value<QColor>().alpha(), 255);
            QVERIFY(aura->property("ready").toBool());
            if (const QString dir = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS"); !dir.isEmpty())
                window->grabWindow().save(QStringLiteral("%1/%2-nowplaying-first-frame.png").arg(dir, QTest::currentDataTag()));
        }
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        shot("nowplaying");
        QMetaObject::invokeMethod(stack, "close");
        QTRY_COMPARE(stack->property("depth").toInt(), 1);

        // A playlist's page, and the sheet of a track from it.
        QMetaObject::invokeMethod(playlist, "libraryRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        auto* library = window->findChild<QQuickItem*>("library");
        QVERIFY(library);
        QMetaObject::invokeMethod(library, "openPlaylist", Q_ARG(QVariant, QStringLiteral("p1")));
        QTRY_COMPARE(stack->property("depth").toInt(), 3);
        QTRY_COMPARE(backend.openTracks()->rowCount(), 2);
        shot("playlist");
        auto* page = window->findChild<QQuickItem*>("playlistScreen");
        QVERIFY(page);
        core.trackScreen().showOpenRow(0);
        QMetaObject::invokeMethod(page, "trackMenuRequested");
        shot("track-sheet");
        QMetaObject::invokeMethod(window->findChild<QObject*>("trackSheet"), "close");
        QMetaObject::invokeMethod(stack, "close");
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        // The Downloads page, and a message over it.
        QMetaObject::invokeMethod(library, "downloadsRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 3);
        core.core().messages().error(QStringLiteral("Something went wrong"));
        QTRY_COMPARE(window->findChild<QObject*>("appToast")->property("isError").toBool(), true);
        shot("downloads");
    }
};

int main(int argc, char** argv)
{
    // The same binary doubles as the backend the tests start.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], Tests::kFakeBackendArgument) == 0)
            return Tests::runFakeBackend();
    }
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("cloudmus-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("cloudmus-mobile-tests"));
    // Anything a test saves goes to throwaway locations, not the user's.
    QStandardPaths::setTestModeEnabled(true);
    SharedCoreTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "SharedCoreTest.moc"
