#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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

class MobileTest : public QObject {
    Q_OBJECT

private slots:
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
        QMetaObject::invokeMethod(playlist, "logRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        shot("log");
        QMetaObject::invokeMethod(stack, "close");
        QTRY_COMPARE(stack->property("depth").toInt(), 1);

        QObject* diagnostics = window->findChild<QObject*>("diagnostics");
        QVERIFY(diagnostics);
        QMetaObject::invokeMethod(diagnostics, "open");
        shot("diagnostics");
    }
};

QTEST_MAIN(MobileTest)
#include "TestMain.moc"
