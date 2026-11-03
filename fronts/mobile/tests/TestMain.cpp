#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <memory>

#include "AuraImageProvider.h"
#include "Backend.h"
#include "Host.h"
#include "LogModel.h"
#include "MobileCore.h"
#include "Platform.h"

#include <QtQml/QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(Cloudmus_MobilePlugin)

namespace {

// A tracks.json naming files that needn't exist: nothing here plays.
App::MobileCore::Paths writeTracks(const QTemporaryDir& dir)
{
    QJsonArray tracks;
    for (int i = 1; i <= 3; ++i) {
        tracks.append(QJsonObject {
            { "id", QStringLiteral("t%1").arg(i) },
            { "file", QStringLiteral("t%1.mp3").arg(i) },
            { "title", QStringLiteral("Title %1").arg(i) },
            { "artist", QStringLiteral("Artist %1").arg(i) },
            { "durationMs", 60000 * i },
            { "cover", QStringLiteral("c%1.jpg").arg(i) },
        });
    }
    QFile file(dir.filePath("tracks.json"));
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(tracks).toJson());
    return { dir.path(), dir.filePath("core.ini") };
}

void writeLog(const QString& path, const QByteArray& text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::Append));
    file.write(text);
}

} // namespace

// A slot for a signal of a replica, which has no member function to point at.
class Receiver : public QObject {
    Q_OBJECT
public slots:
    void run() { emit ran(); }
signals:
    void ran();
};

class MobileTest : public QObject {
    Q_OBJECT

    QTemporaryDir settingsDir_;

private slots:
    // QML Settings (and the core's) go to a fresh directory of the run's:
    // nothing kept from the last run, nothing left in the user's config.
    void initTestCase()
    {
        QVERIFY(settingsDir_.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        QCoreApplication::setOrganizationName(QStringLiteral("cloudmus-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("cloudmus-mobile-tests"));
    }

    void tracksLoadAndRestore()
    {
        QTemporaryDir dir;
        const App::MobileCore::Paths paths = writeTracks(dir);
        {
            App::MobileCore core(paths);
            QCOMPARE(core.activePlaylist().tracks().rowCount(), 3);
            QCOMPARE(core.nowPlaying().title(), QStringLiteral("Title 1"));
            // Saved when the core goes: the next run starts where this left.
            core.player().seek(5000);
        }
        App::MobileCore core(paths);
        QCOMPARE(core.nowPlaying().currentIndex(), 0);
        QCOMPARE(core.nowPlaying().positionMs(), 5000);
        QCOMPARE(core.nowPlaying().playing(), false);
    }

    void replicasMirrorTheCore()
    {
        QTemporaryDir dir;
        App::MobileCore core(writeTracks(dir));
        Remote::Host host(core, dir.filePath("core.sock"));
        QVERIFY(host.listening());

        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);
        QObject* nowPlaying = backend.nowPlaying();
        QCOMPARE(nowPlaying->property("title").toString(), QStringLiteral("Title 1"));
        QCOMPARE(backend.tracks()->rowCount(), 3);

        // A slot called on the replica runs in the core.
        QMetaObject::invokeMethod(backend.playlist(), "play", Q_ARG(int, 2));
        QTRY_COMPARE(core.nowPlaying().currentIndex(), 2);
        QTRY_COMPARE(nowPlaying->property("title").toString(), QStringLiteral("Title 3"));
        QTRY_COMPARE(nowPlaying->property("currentIndex").toInt(), 2);

        QMetaObject::invokeMethod(backend.settings(), "setAutoplay", Q_ARG(bool, false));
        QTRY_COMPARE(core.settings().autoplay(), false);

        QCOMPARE(core.settings().resumeAfterLossSec(), 60);
        QMetaObject::invokeMethod(backend.settings(), "setResumeAfterLossSec", Q_ARG(int, 0));
        QTRY_COMPARE(core.settings().resumeAfterLossSec(), 0);
    }

    // Title, artist and cover of the replica are those of the track that
    // plays, after every change, not those of the one before.
    void replicaShowsTheCurrentTrack()
    {
        QTemporaryDir dir;
        App::MobileCore core(writeTracks(dir));
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);
        QObject* nowPlaying = backend.nowPlaying();

        // What a view sees at the moment `currentIndex` changes: all of it.
        QStringList seen;
        Receiver receiver;
        QObject::connect(nowPlaying, SIGNAL(currentIndexChanged()), &receiver, SLOT(run()));
        QObject::connect(&receiver, &Receiver::ran, [&] {
            seen << QStringLiteral("%1/%2/%3")
                        .arg(nowPlaying->property("currentIndex").toInt())
                        .arg(nowPlaying->property("title").toString(), nowPlaying->property("artist").toString());
        });
        // The position moves all the time while a track plays.
        QTimer position;
        position.setInterval(20);
        QObject::connect(&position, &QTimer::timeout, [&core] { core.player().seek(core.player().positionMs() + 40); });
        position.start();
        for (int index : { 2, 1, 0, 2 }) {
            QMetaObject::invokeMethod(backend.playlist(), "play", Q_ARG(int, index));
            QTRY_COMPARE_WITH_TIMEOUT(nowPlaying->property("currentIndex").toInt(), index, 3000);
            QTRY_COMPARE_WITH_TIMEOUT(
                nowPlaying->property("title").toString(), QStringLiteral("Title %1").arg(index + 1), 3000);
            QTRY_COMPARE_WITH_TIMEOUT(
                nowPlaying->property("artist").toString(), QStringLiteral("Artist %1").arg(index + 1), 3000);
            QTRY_COMPARE_WITH_TIMEOUT(nowPlaying->property("coverUrl").toString(),
                QUrl::fromLocalFile(core.player().tracks()[index].cover).toString(), 3000);
        }
        // And a QML binding to it follows, whatever else changes meanwhile.
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            Item {
                property QtObject np
                readonly property string title: np ? np.title : ""
                readonly property string cover: np ? np.coverUrl : ""
                readonly property string line: np ? np.currentIndex + ":" + np.title + ":" + np.artist + ":" + np.positionMs : ""
            })",
            QUrl());
        std::unique_ptr<QObject> item(component.create());
        QVERIFY2(item, qPrintable(component.errorString()));
        item->setProperty("np", QVariant::fromValue(nowPlaying));
        for (int index : { 1, 0, 2 }) {
            QMetaObject::invokeMethod(backend.playlist(), "play", Q_ARG(int, index));
            QTRY_COMPARE_WITH_TIMEOUT(
                item->property("title").toString(), QStringLiteral("Title %1").arg(index + 1), 3000);
            QTRY_COMPARE_WITH_TIMEOUT(item->property("cover").toString(),
                QUrl::fromLocalFile(core.player().tracks()[index].cover).toString(), 3000);
        }

        // The views are told, for each change; at the last of its signals
        // (properties arrive one by one) what they read is of one track.
        qInfo() << "told" << seen;
        QVERIFY2(seen.size() >= 4, qPrintable(QStringLiteral("told %1 times").arg(seen.size())));
        for (const QString& line : std::as_const(seen)) {
            const QStringList parts = line.split('/');
            QCOMPARE(parts[1], QStringLiteral("Title %1").arg(parts[0].toInt() + 1));
        }
    }

    // The core's process ends (Android kills it with the screen off): the
    // views stop acting on dead replicas, ask for the core again, and are
    // back once it is.
    void replicasFollowTheCoreAway()
    {
        QTemporaryDir dir;
        App::MobileCore core(writeTracks(dir));
        auto host = std::make_unique<Remote::Host>(core, dir.filePath("core.sock"));

        Ui::Backend backend(dir.filePath("core.sock"));
        int restarts = 0;
        backend.setRestartCore([&restarts] { ++restarts; });
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);

        host.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.ready(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(restarts > 0, 5000);

        host = std::make_unique<Remote::Host>(core, dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 10000);
        QMetaObject::invokeMethod(backend.playlist(), "play", Q_ARG(int, 1));
        QTRY_COMPARE(core.nowPlaying().currentIndex(), 1);
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
        QTest::newRow("phone") << QSize(412, 892);
        QTest::newRow("headunit") << QSize(1920, 1032);
    }
    void pagesOpen()
    {
        QFETCH(QSize, size);
        QTemporaryDir dir;
        App::MobileCore core({ QStringLiteral(CLOUDMUS_MOBILE_MEDIA_DIR), dir.filePath("core.ini") });
        Ui::Backend backend(core);
        Ui::Platform platform(dir.path());

        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("aura"), new Ui::AuraImageProvider);
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
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        auto shot = [&](const char* name) {
            QTest::qWait(700); // transitions, async covers
            if (!shots.isEmpty()) {
                QDir().mkpath(shots);
                window->grabWindow().save(QStringLiteral("%1/%2-%3.png").arg(shots, QTest::currentDataTag(), name));
            }
        };
        shot("playlist");

        auto* playlist = window->findChild<QQuickItem*>("playlist");
        QVERIFY(playlist);
        QMetaObject::invokeMethod(playlist, "nowPlayingRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        shot("nowplaying");

        QMetaObject::invokeMethod(stack, "close");
        QTRY_COMPARE(stack->property("depth").toInt(), 1);
        // Settings, a page further the developer tools, and from there the log.
        QMetaObject::invokeMethod(playlist, "settingsRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        shot("settings");
        auto* settings = window->findChild<QQuickItem*>("settings");
        QVERIFY(settings);
        QMetaObject::invokeMethod(settings, "developerRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 3);
        shot("developer");
        auto* developer = window->findChild<QQuickItem*>("developer");
        QVERIFY(developer);
        QMetaObject::invokeMethod(developer, "logRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 4);
        shot("log");

        // Back, a page at a time, to the playlist.
        for (int depth = 3; depth >= 1; --depth) {
            QMetaObject::invokeMethod(stack, "close");
            QTRY_COMPARE(stack->property("depth").toInt(), depth);
        }
    }
};

QTEST_MAIN(MobileTest)
#include "TestMain.moc"
