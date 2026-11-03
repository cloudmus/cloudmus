#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

#include "Backend.h"
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
        shot("playlist");
        QMetaObject::invokeMethod(playlist, "nowPlayingRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        shot("nowplaying");
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
