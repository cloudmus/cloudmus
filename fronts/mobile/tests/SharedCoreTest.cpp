#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>

#include "AuraImageProvider.h"
#include "Backend.h"
#include "CoverImageProvider.h"
#include "FakeBackend.h"
#include "Host.h"
#include "LogModel.h"
#include "MobileCore.h"
#include "Platform.h"
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
            QVERIFY(!sceneRect(bar).intersects(normalCover));
            QVERIFY(!railSettings->isVisible());
            QVERIFY(sceneRect(details).top() > normalCover.top());

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
        QMetaObject::invokeMethod(playlist, "nowPlayingRequested");
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
