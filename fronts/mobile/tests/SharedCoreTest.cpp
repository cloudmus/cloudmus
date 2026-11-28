#include <QAbstractItemModel>
#include <QColor>
#include <QDir>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QDateTime>
#include <QSettings>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QStyleHints>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QScopeGuard>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>

#include <QClipboard>

#include "AuraImageProvider.h"
#include "Backend.h"
#include "CoverImageProvider.h"
#include "FakeBackend.h"
#include "ImitatedMusic.h"
#include "Host.h"
#include "Localizer.h"
#include "LogoSound.h"
#include "LogModel.h"
#include "MobileCore.h"
#include "Platform.h"
#include "PulseScreen.h"
#include "QrImageProvider.h"
#include "QrPicture.h"
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

// By what's shown inside what: a Repeater's items aren't its page's QObject
// children, which findChild() goes by.
QQuickItem* findItem(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name)
        return root;
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = findItem(child, name))
            return found;
    }
    return nullptr;
}

// The start screen gone from over the pages (StartSplash.qml): they're
// drawn, at their own size, once the core is up and it has faded.
AuthMethod codeMethod(const QString& code, std::optional<int> expiresInSec = std::nullopt)
{
    AuthMethod method;
    method.kind = QStringLiteral("code");
    method.url = QStringLiteral("https://ya.ru/device");
    method.code = code;
    method.expiresInSec = expiresInSec;
    return method;
}

bool startScreenGone(QQuickWindow* window)
{
    auto* splash = window->findChild<QQuickItem*>(QStringLiteral("startSplash"));
    return splash && QTest::qWaitFor([&] { return !splash->isVisible(); }, 10000);
}

void forEachItem(QQuickItem* root, const std::function<void(QQuickItem*)>& visit)
{
    visit(root);
    for (QQuickItem* child : root->childItems())
        forEachItem(child, visit);
}

} // namespace

// The QR provider, counting what it draws and noting where.
class CountingQrProvider : public Ui::QrImageProvider {
public:
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        ++draws;
        if (QThread::currentThread() == QCoreApplication::instance()->thread())
            ++drawsOnUiThread;
        return Ui::QrImageProvider::requestImage(id, size, requestedSize);
    }
    static inline std::atomic_int draws = 0;
    static inline std::atomic_int drawsOnUiThread = 0;
};

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
    // The picture itself is the core's (QrPictureTest); here, what QrCode.qml
    // asks the provider for.
    void qrProviderDrawsThePercentEncodedText()
    {
        Ui::QrImageProvider provider;
        QSize size;
        const QImage image = provider.requestImage(
            QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("https://ya.ru/device"))), &size, QSize(240, 240));
        QCOMPARE(size, QSize(240, 240));
        QCOMPARE(image, Qr::picture(QStringLiteral("https://ya.ru/device"), 240));
    }

    // A panel opens and folds over a moment, taking its layout's spacing with it.
    void panelsComeAndGoSmoothly()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Layouts
            import Cloudmus.Mobile
            Window {
                width: 300
                height: 600
                visible: true
                property alias column: column
                property alias panel: panel
                property alias row: row
                property real extra: 0
                ColumnLayout {
                    id: column
                    width: parent.width
                    spacing: 16
                    Panel {
                        id: panel
                        shown: false
                        Item { implicitHeight: 100 + extra }
                    }
                    Item { implicitHeight: 10 }
                    SettingsRow { id: row; text: "Row" }
                }
            }
        )",
            { });
        const std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(object.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* column = object->property("column").value<QQuickItem*>();
        auto* panel = object->property("panel").value<QQuickItem*>();
        auto* row = object->property("row").value<QQuickItem*>();
        QTest::qWait(300);
        QVERIFY(!panel->isVisible());
        const qreal without = column->implicitHeight();

        // Coming in: part of the way first, then all of it.
        panel->setProperty("shown", true);
        const qreal full = panel->property("padding").toReal() * 2 + 100;
        QTRY_VERIFY(panel->height() > 0 && panel->height() < full);
        // To the very end: the layout's rounded height gets there sooner.
        QTRY_COMPARE(panel->implicitHeight(), full);
        QTRY_COMPARE(column->implicitHeight(), without + full + 16);

        // Growing.
        object->setProperty("extra", 80);
        QTRY_VERIFY(panel->height() > full && panel->height() < full + 80);
        QTRY_COMPARE(panel->height(), full + 80);

        // Going, as a row does.
        const qreal rowHeight = row->height();
        row->setProperty("shown", false);
        QTRY_VERIFY(row->height() > 0 && row->height() < rowHeight);
        QTRY_VERIFY(!row->isVisible());
        panel->setProperty("shown", false);
        QTRY_VERIFY(!panel->isVisible());
        QTRY_COMPARE(column->implicitHeight(), 10.0);
    }

    void initTestCase()
    {
        QVERIFY(settingsDir_.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        // The first launch's slides would be over every page; their own test
        // starts without this.
        QSettings().setValue(QStringLiteral("ui/onboardingSeen"), true);
        Tests::installFakeBackendManifest();
    }

    // What plays reaches the views in another process: the rows, the current
    // one, and the track's own properties, each after every change.
    void replicasFollowTheCore()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QTRY_VERIFY_WITH_TIMEOUT(core.core().playback().hasCurrentTrack(), 10000);
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
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QTRY_VERIFY(core.core().playback().hasCurrentTrack());
        QTRY_COMPARE(backend.tracks()->rowCount(), 2);
        // An earlier test's saved track may be what it went on with.
        QMetaObject::invokeMethod(backend.playlist(), "playRow", Q_ARG(int, 0));
        QTRY_COMPARE(core.core().playback().currentIndex(), 0);
        const QString track = core.nowPlaying().title();

        QMetaObject::invokeMethod(backend.playlist(), "playRow", Q_ARG(int, 1));
        QTRY_COMPARE(core.core().playback().currentIndex(), 1);
        QMetaObject::invokeMethod(library, "playPlaylist", Q_ARG(QString, QStringLiteral("p1")));
        QTRY_COMPARE(spy.count(), 2);
        QCOMPARE(core.core().playback().currentIndex(), 1);
        QVERIFY(core.nowPlaying().title() != track);
    }

    // What a car's Media Center lists: the source as a folder, even the one
    // source (its tabs are the root's folders), its playlists in it,
    // favorites first, and a tap starts one.
    void mediaBrowsersListThePlaylists()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
        ViewModel::MediaTree& tree = core.mediaTree();
        QSignalSpy changed(&tree, &ViewModel::MediaTree::changed);
        QTRY_COMPARE_WITH_TIMEOUT(tree.children(QStringLiteral("source/fake")).size(), 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 5000);

        core.library().selectSource(QStringLiteral("fake"));
        if (!core.core().sources().isFavorite(QStringLiteral("fake"), QStringLiteral("p2")))
            core.library().toggleFavorite(QStringLiteral("p2"));
        if (core.core().sources().isFavorite(QStringLiteral("fake"), QStringLiteral("p1")))
            core.library().toggleFavorite(QStringLiteral("p1"));
        const QList<ViewModel::MediaTree::Item> root = tree.children(QLatin1String(ViewModel::MediaTree::kRootId));
        QCOMPARE(root.size(), 1);
        QCOMPARE(root.first().id, QStringLiteral("source/fake"));
        QVERIFY(root.first().browsable);
        const QList<ViewModel::MediaTree::Item> items = tree.children(QStringLiteral("source/fake"));
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.first().id, QStringLiteral("playlist/fake/p2"));
        for (const ViewModel::MediaTree::Item& item : items) {
            QVERIFY(!item.browsable);
            QVERIFY(!item.title.isEmpty());
            QCOMPARE(tree.item(item.id), item);
        }
        QCOMPARE(tree.item(QStringLiteral("source/fake")).title, QStringLiteral("Fake Source"));
        QVERIFY(tree.item(QStringLiteral("source/nothing")).id.isEmpty());
        QVERIFY(tree.item(QStringLiteral("playlist/fake/nothing")).id.isEmpty());
        QVERIFY(tree.item(QStringLiteral("nonsense")).id.isEmpty());
        QVERIFY(tree.children(items.first().id).isEmpty());

        // A car's search: every word, whatever the case.
        QCOMPARE(tree.search(QStringLiteral("sav")).size(), 1);
        QCOMPARE(tree.search(QStringLiteral("  SAVED ")).first().id, QStringLiteral("playlist/fake/p2"));
        QCOMPARE(tree.search(QStringLiteral("fir xyz")).size(), 0); // every word, not any
        QCOMPARE(tree.search(QStringLiteral("s")).size(), 2); // "First", "Saved": favorites first
        QCOMPARE(tree.search(QStringLiteral("s")).first().id, QStringLiteral("playlist/fake/p2"));
        QVERIFY(tree.search(QStringLiteral("nothing like it")).isEmpty());
        QVERIFY(tree.search(QStringLiteral("   ")).isEmpty());

        // Asked again only when something else is listed.
        QTest::qWait(1500); // what the toggles above said is told by now
        changed.clear();
        QTest::qWait(1500);
        QCOMPARE(changed.count(), 0);
        core.library().toggleFavorite(QStringLiteral("p2"));
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 5000);

        tree.play(QStringLiteral("playlist/fake/p1"));
        QTRY_VERIFY_WITH_TIMEOUT(core.core().playback().hasCurrentTrack(), 10000);
        QCOMPARE(core.activePlaylist().context().playlist.id, QStringLiteral("p1"));

        // With a queue, search finds its tracks too, before the playlists.
        const QString title = core.nowPlaying().title();
        const QList<ViewModel::MediaTree::Item> found = tree.search(title.toUpper());
        QVERIFY(!found.isEmpty());
        QVERIFY(found.first().id.startsWith(QLatin1String(ViewModel::MediaTree::kQueuePrefix)));
        const Playback::PlaybackController& playback = core.core().playback();
        const QString playing = QStringLiteral("queue/%1/fake/%2")
                                    .arg(playback.currentIndex())
                                    .arg(playback.queue().at(playback.currentIndex()).track.id);
        const auto it = std::find_if(found.cbegin(), found.cend(), [&](const auto& item) { return item.id == playing; });
        QVERIFY(it != found.cend());
        QCOMPARE(it->title, title);
        QVERIFY(!it->browsable);
        QCOMPARE(tree.search(QStringLiteral("saved")).first().id, QStringLiteral("playlist/fake/p2"));
        // Nonsense does nothing.
        tree.play(QStringLiteral("playlist/fake/nothing"));
        tree.play(QStringLiteral("nonsense"));
        QCOMPARE(core.activePlaylist().context().playlist.id, QStringLiteral("p1"));
    }

    // A car's Media Center lists what's saved the moment the car starts, and
    // a tap on it can't be lost for the source not being up yet.
    void aPickInAMediaBrowserWaitsForItsSource()
    {
        QTemporaryDir dir;
        const App::MobileCore::Paths paths { dir.filePath("core.ini"), 500, 0 };
        {
            App::MobileCore core(paths);
            QTRY_VERIFY_WITH_TIMEOUT(!core.mediaTree().children(QStringLiteral("root")).isEmpty(), 10000);
        }
        App::MobileCore core(paths);
        QVERIFY(core.core().sourceManager().client(QStringLiteral("fake")) == nullptr);
        QVERIFY(!core.mediaTree().children(QStringLiteral("root")).isEmpty());
        core.mediaTree().play(QStringLiteral("playlist/fake/p1"));
        QVERIFY(!core.core().playback().hasCurrentTrack());
        QTRY_VERIFY_WITH_TIMEOUT(core.core().playback().hasCurrentTrack(), 10000);
        QCOMPARE(core.activePlaylist().context().playlist.id, QStringLiteral("p1"));
    }

    // The settings as a phone reaches them: over a replica, applied at once,
    // with a source's own form and its restart.
    void settingsThroughAReplica()
    {
        qputenv("CLOUDMUS_FAKE_SETTINGS", "1");
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
        // The config file outlives a test.
        core.core().settings().setProxies({ });
        core.core().settings().setSourceConnection(QStringLiteral("fake"), QLatin1String(Config::Settings::kSystemConnection));
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);
        const QString fake = QStringLiteral("fake");
        QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(fake) != nullptr
                && core.core().sourceManager().client(fake)->available(),
            10000);
        qunsetenv("CLOUDMUS_FAKE_SETTINGS");

        QObject* screen = backend.settingsScreen();
        const int tracks = core.core().preload().playlistTracks() + 1;
        QMetaObject::invokeMethod(screen, "setPlaylistTracks", Q_ARG(int, tracks));
        QTRY_COMPARE(core.core().settings().preloadPlaylistTracks(), tracks);
        QTRY_COMPARE(screen->property("playlistTracks").toInt(), tracks);
        QMetaObject::invokeMethod(screen, "setCacheMaxMb", Q_ARG(int, 512));
        QTRY_COMPARE(screen->property("cacheMaxMb").toInt(), 512);
        const bool analytics = core.core().settings().analyticsEnabled();
        QMetaObject::invokeMethod(screen, "setAnalytics", Q_ARG(bool, !analytics));
        QTRY_COMPARE(screen->property("analytics").toBool(), !analytics);
        QMetaObject::invokeMethod(screen, "setAnalytics", Q_ARG(bool, analytics));
        QVERIFY(!screen->property("cacheUsedText").toString().isEmpty());

        QAbstractItemModel* sources = backend.settingsSources();
        QTRY_COMPARE(sources->rowCount(), 1);
        QTRY_COMPARE(sources->data(sources->index(0, 0), roleOf(*sources, "name")).toString(), QStringLiteral("Fake Source"));

        // A proxy: saved, listed, offered to the source.
        QAbstractItemModel* proxies = backend.settingsProxies();
        QMetaObject::invokeMethod(screen, "saveProxy", Q_ARG(QString, QString()), Q_ARG(QString, QStringLiteral("Home")),
            Q_ARG(QString, QStringLiteral("socks5")), Q_ARG(QString, QStringLiteral("10.0.0.1")), Q_ARG(int, 1081),
            Q_ARG(QString, QStringLiteral("me")), Q_ARG(QString, QStringLiteral("secret")));
        QTRY_COMPARE(proxies->rowCount(), 1);
        const Config::ProxyConfig saved = core.core().settings().proxies().first();
        QCOMPARE(saved.type, Config::ProxyConfig::Type::Socks5);
        QCOMPARE(saved.port, 1081);
        QTRY_COMPARE(proxies->data(proxies->index(0, 0), roleOf(*proxies, "kind")).toString(), QStringLiteral("socks5"));

        QObject* page = backend.sourceSettings();
        QAbstractItemModel* connections = backend.sourceConnections();
        QAbstractItemModel* fields = backend.sourceFields();
        QMetaObject::invokeMethod(page, "open", Q_ARG(QString, fake));
        QTRY_COMPARE(page->property("name").toString(), QStringLiteral("Fake Source"));
        QTRY_COMPARE(connections->rowCount(), 3);
        QTRY_COMPARE(connections->data(connections->index(0, 0), roleOf(*connections, "isCurrent")).toBool(), true);
        QTRY_COMPARE_WITH_TIMEOUT(fields->rowCount(), 5, 5000);
        const auto field = [&](int row, const char* role) { return fields->data(fields->index(row, 0), roleOf(*fields, role)); };
        // The untitled group first, its heading on the first of a titled one.
        QTRY_COMPARE(field(0, "key").toString(), QStringLiteral("quality"));
        QTRY_COMPARE(field(1, "groupTitle").toString(), QStringLiteral("More"));
        QTRY_COMPARE(field(2, "groupTitle").toString(), QString());
        QTRY_COMPARE(field(0, "options").toList().size(), 2);
        QTRY_COMPARE(field(0, "options").toList().first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("High"));

        // What the source turns down says so in its row, and goes when it's taken.
        QMetaObject::invokeMethod(page, "setFieldText", Q_ARG(QString, QStringLiteral("name")), Q_ARG(QString, QStringLiteral("bad")));
        QTRY_COMPARE(field(3, "error").toString(), QStringLiteral("Not that name"));
        QMetaObject::invokeMethod(page, "setFieldText", Q_ARG(QString, QStringLiteral("name")), Q_ARG(QString, QStringLiteral(" good ")));
        QTRY_COMPARE(field(3, "error").toString(), QString());
        QTRY_COMPARE(field(3, "value").toString(), QStringLiteral("good"));
        // A secret isn't sent back or shown.
        QVERIFY(!field(4, "isSet").toBool());
        QMetaObject::invokeMethod(page, "setFieldText", Q_ARG(QString, QStringLiteral("token")), Q_ARG(QString, QStringLiteral("t")));
        QTRY_VERIFY(field(4, "isSet").toBool());
        QVERIFY(field(4, "value").toString().isEmpty());

        // A setting that takes a restart: the source comes back, and its form.
        const Rpc::RpcClient* before = core.core().sourceManager().client(fake);
        QSignalSpy ready(&core.core().sourceManager(), &Rpc::SourceManager::sourceReady);
        qputenv("CLOUDMUS_FAKE_SETTINGS", "1");
        QMetaObject::invokeMethod(page, "setFieldBool", Q_ARG(QString, QStringLiteral("flag")), Q_ARG(bool, true));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(fields->rowCount(), 5, 5000);

        // A connection: saved, and the source restarts on it.
        ready.clear();
        QMetaObject::invokeMethod(page, "setConnection", Q_ARG(QString, saved.id));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        QCOMPARE(core.core().settings().sourceConnection(fake), saved.id);
        QTRY_COMPARE(connections->data(connections->index(2, 0), roleOf(*connections, "isCurrent")).toBool(), true);
        // Its proxy goes: it falls back, says so, and restarts.
        ready.clear();
        QMetaObject::invokeMethod(screen, "removeProxy", Q_ARG(QString, saved.id));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        QTRY_COMPARE(proxies->rowCount(), 0);
        qunsetenv("CLOUDMUS_FAKE_SETTINGS");
        QTRY_VERIFY(page->property("connectionHint").toString().contains(QStringLiteral("removed")));
        QTRY_COMPARE(connections->data(connections->index(0, 0), roleOf(*connections, "isCurrent")).toBool(), true);

        // Off, it's off for good; on, it comes back.
        QMetaObject::invokeMethod(page, "setEnabled", Q_ARG(bool, false));
        QTRY_VERIFY(!core.core().sourceManager().isEnabled(fake));
        QTRY_COMPARE(page->property("enabled").toBool(), false);
        QTRY_VERIFY(!page->property("status").toString().isEmpty());
        QTRY_COMPARE(sources->data(sources->index(0, 0), roleOf(*sources, "status")).toString(), QStringLiteral("Off"));
        ready.clear();
        QMetaObject::invokeMethod(page, "setEnabled", Q_ARG(bool, true));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    }

    // The language: chosen in the views, saved by the core, in force in both —
    // the views' own texts and the ones the core builds — and "Auto" follows
    // the system again.
    void languageSwitches()
    {
        QTemporaryDir dir;
        // The core's language is saved in the shared test config, the views'
        // own copy next to the app's: leave neither behind.
        const auto forget = [] {
            Config::Settings().setLanguage(QString());
            QSettings().remove(QStringLiteral("ui/language"));
        };
        forget();
        const auto guard = qScopeGuard(forget);
        App::MobileCore core({ dir.filePath("core.ini") });
        Ui::Backend backend(core);
        QQmlEngine engine;
        Ui::Localizer localizer(backend, engine);
        QVERIFY(localizer.languages().size() > 1);
        QCOMPARE(QCoreApplication::translate("SettingsPage", "Language"), QStringLiteral("Language"));

        QSignalSpy changed(&localizer, &Ui::Localizer::settingChanged);
        localizer.setSetting(QStringLiteral("ru"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(QCoreApplication::translate("SettingsPage", "Language"), QStringLiteral("Язык"));
        QCOMPARE(core.core().settings().language(), QStringLiteral("ru"));
        QCOMPARE(core.core().translator().language(), QStringLiteral("ru"));
        // A text the desktop has too reads the same.
        QCOMPARE(QCoreApplication::translate("SignInCard", "Sign in"), QStringLiteral("Войти"));

        // The system's language is what Auto means, as the views see it.
        core.settingsScreen().setSystemLanguages({ QStringLiteral("de-DE") });
        localizer.setSetting(QString());
        QCOMPARE(core.core().settings().language(), QString());
        QCOMPARE(core.core().translator().language(), QStringLiteral("de"));
        QCOMPARE(QCoreApplication::translate("SettingsPage", "Language"), QStringLiteral("Sprache"));
    }

    // The pages on a phone: all of it can be reached and nothing runs off the
    // side, in the narrowest there is.
    void settingsPagesFitAPhone()
    {
        qputenv("CLOUDMUS_FAKE_SETTINGS", "1");
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
        core.core().settings().setProxies({ }); // the config file outlives a test
        Ui::Backend backend(core);
        Ui::Platform platform(dir.path());
        const QString fake = QStringLiteral("fake");
        QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(fake) != nullptr
                && core.core().sourceManager().client(fake)->available(),
            10000);
        qunsetenv("CLOUDMUS_FAKE_SETTINGS");

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
        QVERIFY(startScreenGone(window));
        auto* theme = engine.singletonInstance<QObject*>("Cloudmus.Mobile", "Theme");
        QVERIFY(theme);
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        const auto shot = [&](const char* name) {
            if (!shots.isEmpty()) {
                QTest::qWait(500);
                window->grabWindow().save(shots + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".png"));
            }
        };
        const auto fits = [&](QQuickItem* page) {
            QVERIFY(page != nullptr);
            forEachItem(page, [&](QQuickItem* item) {
                if (!item->isVisible() || item->objectName().isEmpty())
                    return;
                const QRectF rect = item->mapRectToScene(item->boundingRect());
                QVERIFY2(rect.left() >= 0 && rect.right() <= window->width() + 0.5, qPrintable(item->objectName()));
            });
        };
        const auto press = [&](QQuickItem* page, const char* name) {
            auto* item = findItem(page, QLatin1String(name));
            QVERIFY2(item != nullptr, name);
            QVERIFY2(QMetaObject::invokeMethod(item, "clicked"), name);
        };

        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "settingsRequested"));
        QTRY_VERIFY(window->findChild<QQuickItem*>("settings") != nullptr);
        auto* settings = window->findChild<QQuickItem*>("settings");
        QTRY_VERIFY(findItem(settings, QStringLiteral("sourceRow_fake")) != nullptr);
        QTest::qWait(500); // it slides in
        fits(settings);
        shot("settings");

        // The theme: the user's, or the system's, at once.
        const QColor pageColor = theme->property("surface0").value<QColor>();
        press(settings, "scheme_light");
        QTRY_VERIFY(findItem(settings, QStringLiteral("scheme_light"))->property("checked").toBool());
        QCOMPARE(theme->property("dark").toBool(), false);
        // Black until it was renamed: QML takes "on..." for a signal handler.
        QCOMPARE(theme->property("inkOnAccent").value<QColor>(), QColor(Qt::white));
        const QColor lightPage = theme->property("surface0").value<QColor>();
        press(settings, "scheme_dark");
        QCOMPARE(theme->property("dark").toBool(), true);
        QVERIFY(theme->property("surface0").value<QColor>() != lightPage);
        QVERIFY(!findItem(settings, QStringLiteral("scheme_light"))->property("checked").toBool());
        press(settings, "scheme_auto");
        QCOMPARE(theme->property("surface0").value<QColor>(), pageColor); // the config file outlives a test
        const auto systemDark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
        QCOMPARE(theme->property("dark").toBool(), systemDark);

        // A step at a time, and no further than it goes.
        auto* cache = findItem(settings, QStringLiteral("cacheSize"));
        QVERIFY(cache);
        const int megabytes = core.core().preload().cacheMaxMb();
        QVERIFY(QMetaObject::invokeMethod(findItem(cache, QStringLiteral("stepUp")), "clicked"));
        QCOMPARE(core.core().preload().cacheMaxMb(), megabytes + 256);

        // A source's page, with its own form.
        press(settings, "sourceRow_fake");
        QTRY_VERIFY(window->findChild<QQuickItem*>("sourceSettings") != nullptr);
        auto* source = window->findChild<QQuickItem*>("sourceSettings");
        QTRY_VERIFY_WITH_TIMEOUT(findItem(source, QStringLiteral("option_low")) != nullptr, 5000);
        QTest::qWait(400);
        fits(source);
        shot("source-settings");
        press(source, "option_low");
        QTRY_VERIFY(findItem(source, QStringLiteral("option_low"))->property("checked").toBool());
        QTRY_VERIFY(!findItem(source, QStringLiteral("option_high"))->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QObject*>("stack"), "close"));
        QTRY_VERIFY(window->findChild<QQuickItem*>("sourceSettings") == nullptr);

        // A new proxy: not without a host; then listed, and open to edit.
        press(settings, "addProxy");
        QTRY_VERIFY(window->findChild<QQuickItem*>("proxy") != nullptr);
        auto* proxy = window->findChild<QQuickItem*>("proxy");
        QTest::qWait(400);
        fits(proxy);
        shot("proxy");
        auto* save = findItem(proxy, QStringLiteral("proxySave"));
        QVERIFY(save);
        QVERIFY(!save->isEnabled());
        QVERIFY(findItem(proxy, QStringLiteral("proxyHost"))->setProperty("text", QStringLiteral("10.0.0.1")));
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(!findItem(proxy, QStringLiteral("proxyRemove"))->isVisible());
        press(proxy, "proxySave");
        QTRY_VERIFY(window->findChild<QQuickItem*>("proxy") == nullptr);
        QCOMPARE(core.core().settings().proxies().first().host, QStringLiteral("10.0.0.1"));
        QCOMPARE(core.core().settings().proxies().first().port, 1080);
        QTRY_VERIFY(findItem(settings, QStringLiteral("proxyRow_10.0.0.1")) != nullptr);
        press(settings, "proxyRow_10.0.0.1");
        QTRY_VERIFY(window->findChild<QQuickItem*>("proxy") != nullptr);
        proxy = window->findChild<QQuickItem*>("proxy");
        QTRY_COMPARE(findItem(proxy, QStringLiteral("proxyHost"))->property("text").toString(), QStringLiteral("10.0.0.1"));
        QVERIFY(findItem(proxy, QStringLiteral("proxyRemove"))->isVisible());
        press(proxy, "proxyRemove");
        QTRY_VERIFY(core.core().settings().proxies().isEmpty());
    }

    // On a phone Now Playing follows the finger: up from the mini player, down
    // from itself, staying where a short swipe leaves it.
    void nowPlayingFollowsTheFinger()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
        Ui::Backend backend(core);
        Ui::Platform platform(dir.path());
        playFake(core);
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
        window->resize(420, 860);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        auto* sheet = window->findChild<QQuickItem*>("nowPlayingSheet");
        QVERIFY(sheet);
        // The mini player is Now Playing folded up: one page, all along.
        QTRY_VERIFY(window->findChild<QQuickItem*>("nowPlaying") != nullptr);
        auto* page = window->findChild<QQuickItem*>("nowPlaying");
        QTRY_VERIFY(findItem(page, QStringLiteral("trackTitle")) != nullptr);
        QTRY_VERIFY(findItem(page, QStringLiteral("trackTitle"))->isVisible());
        const auto progress = [&] { return sheet->property("progress").toReal(); };
        // Slowly, so that it's the distance that counts.
        const auto drag = [&](int x, int fromY, int toY, bool release) {
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(x, fromY));
            const int steps = 12;
            for (int i = 1; i <= steps; ++i) {
                QTest::qWait(30);
                QTest::mouseMove(window, QPoint(x, fromY + (toY - fromY) * i / steps));
            }
            QTest::qWait(250);
            QTest::mouseMove(window, QPoint(x, toY));
            if (release)
                QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(x, toY));
        };
        const int miniY = 860 - 36;
        const int titleX = 200;

        // The mini player's controls, in the bar at the bottom: what's asked of the core.
        auto* surface = findItem(page, QStringLiteral("nowPlayingSurface"));
        QVERIFY(surface);
        QCOMPARE(surface->y(), 860.0 - 124);
        for (const char* name : { "transportLike", "transportPrevious", "transportPlay", "transportNext", "transportDislike" }) {
            auto* button = findItem(page, QLatin1String(name));
            QVERIFY2(button && button->isVisible(), name);
            const QRectF at = button->mapRectToScene(button->boundingRect());
            QVERIFY2(at.top() >= 860 - 124 && at.bottom() <= 860, name);
        }
        // Shuffle and repeat, top right, right of the title.
        {
            auto* title = findItem(page, QStringLiteral("trackTitle"));
            const QRectF titleAt = title->mapRectToScene(title->boundingRect());
            for (const char* name : { "shuffleButton", "repeatButton" }) {
                auto* button = findItem(page, QLatin1String(name));
                QVERIFY2(button, name);
                QTRY_VERIFY2(button->isVisible() && button->isEnabled(), name);
                const QRectF at = button->mapRectToScene(button->boundingRect());
                QVERIFY2(at.top() >= 860 - 124 && at.bottom() <= 860 - 124 + 70 && at.right() <= 420, name);
                QVERIFY2(at.left() >= titleAt.left() + title->width() * title->scale() - 1, name);
            }
            const int repeat = core.nowPlaying().repeat();
            QVERIFY(QMetaObject::invokeMethod(findItem(page, QStringLiteral("repeatButton")), "clicked"));
            QTRY_VERIFY(core.nowPlaying().repeat() != repeat);
            while (core.nowPlaying().repeat() != repeat)
                core.nowPlaying().cycleRepeat();
        }
        // The cover, small, at the bar's left.
        {
            auto* cover = findItem(page, QStringLiteral("trackCover"));
            const QRectF at = cover->mapRectToScene(cover->boundingRect());
            QVERIFY2(qAbs(at.width() - 48) < 1 && at.top() >= 860 - 124 && at.left() < 30,
                qPrintable(QStringLiteral("%1,%2 %3").arg(at.left()).arg(at.top()).arg(at.width())));
        }
        QTRY_VERIFY(findItem(page, QStringLiteral("transportLike"))->isEnabled());
        // From whichever way it is: what's saved of a track outlives a test.
        const bool liked = core.nowPlaying().liked();
        QVERIFY(QMetaObject::invokeMethod(findItem(page, QStringLiteral("transportLike")), "clicked"));
        QTRY_COMPARE(core.nowPlaying().liked(), !liked);
        QTRY_COMPARE(findItem(page, QStringLiteral("transportLike"))->property("iconName").toString(),
            liked ? QStringLiteral("favorite_border") : QStringLiteral("favorite"));
        QVERIFY(QMetaObject::invokeMethod(findItem(page, QStringLiteral("transportLike")), "clicked"));
        QTRY_COMPARE(core.nowPlaying().liked(), liked);

        // Follows, then falls back from a short pull: what's in the bar grows with it.
        drag(titleX, miniY, miniY - 100, false);
        QVERIFY(progress() > 0.05 && progress() < 0.2);
        QVERIFY(surface->y() < 860 - 124 - 80);
        QVERIFY(findItem(page, QStringLiteral("trackCover"))->width() > 48);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(titleX, miniY - 100));
        QTRY_COMPARE(progress(), 0.0);

        // The rest of the way from a long one.
        drag(titleX, miniY, miniY - 300, true);
        QTRY_COMPARE(progress(), 1.0);
        // All of the screen, the cover as large as the width allows.
        QCOMPARE(surface->y(), 0.0);
        // The same buttons, moved to the panel's slots.
        QVERIFY(findItem(page, QStringLiteral("shuffleButton"))->isVisible());
        QVERIFY(findItem(page, QStringLiteral("trackCover"))->width() > 300);
        // It says what it plays from.
        auto* playlistName = findItem(page, QStringLiteral("nowPlayingPlaylist"));
        QVERIFY(playlistName);
        QTRY_COMPARE(playlistName->property("text").toString(), core.activePlaylist().title());
        QVERIFY(!playlistName->property("text").toString().isEmpty());

        // Down from the top of it: a short push goes back up...
        drag(210, 100, 180, true);
        QTRY_COMPARE(progress(), 1.0);
        // ...and a long one closes it.
        drag(210, 100, 400, true);
        QTRY_COMPARE(progress(), 0.0);

        // On the way, for a look: CLOUDMUS_MOBILE_SHOTS.
        if (const QString dir = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS"); !dir.isEmpty()) {
            for (const qreal at : { 0.0, 0.25, 0.5, 0.75, 1.0 }) {
                QMetaObject::invokeMethod(sheet, "follow", Q_ARG(QVariant, at));
                QTest::qWait(100);
                window->grabWindow().save(QStringLiteral("%1/nowplaying-at-%2.png").arg(dir).arg(int(at * 100)));
            }
        }

        // Closing folds it back into the bar, going no further down than that.
        QMetaObject::invokeMethod(sheet, "open");
        QTRY_COMPARE(progress(), 1.0);
        QMetaObject::invokeMethod(sheet, "close");
        qreal lowest = 0;
        for (int i = 0; i < 80 && progress() > 0; ++i) {
            lowest = std::max(lowest, surface->y());
            QTest::qWait(8);
        }
        QVERIFY2(lowest <= 860 - 124, qPrintable(QString::number(lowest)));
        QTRY_COMPARE(surface->y(), 860.0 - 124);

        // A track's sheet goes the same way, and a press on a row in it isn't
        // taken for a press of the row.
        core.trackScreen().showActiveRow(0);
        auto* trackSheet = window->findChild<QObject*>("trackSheet");
        QVERIFY(trackSheet);
        QVERIFY(QMetaObject::invokeMethod(trackSheet, "open"));
        QTRY_VERIFY(trackSheet->property("opened").toBool());
        const auto sheetTop = [&] { return 860 - trackSheet->property("height").toInt(); };
        QTRY_VERIFY(sheetTop() < 800);
        const QString track = core.nowPlaying().title();
        // A finger, as a phone has: a mouse moves differently under a popup.
        std::unique_ptr<QPointingDevice> finger(QTest::createTouchDevice());
        const auto swipe = [&](int fromY, int toY, bool release) {
            QTest::touchEvent(window, finger.get()).press(0, QPoint(300, fromY), window);
            const int steps = 12;
            for (int i = 1; i <= steps; ++i) {
                QTest::qWait(30);
                QTest::touchEvent(window, finger.get()).move(0, QPoint(300, fromY + (toY - fromY) * i / steps), window);
            }
            QTest::qWait(250);
            if (release)
                QTest::touchEvent(window, finger.get()).release(0, QPoint(300, toY), window);
        };
        // From the part with nothing to press, a short way: back up.
        const int inSheet = sheetTop() + 60;
        swipe(inSheet, inSheet + 60, false);
        QVERIFY(trackSheet->property("slide").toReal() > 0);
        QTest::touchEvent(window, finger.get()).release(0, QPoint(300, inSheet + 60), window);
        QTRY_COMPARE(trackSheet->property("slide").toReal(), 0.0);
        QVERIFY(trackSheet->property("opened").toBool());
        // Starting on a row, all the way: it goes, and the row isn't pressed.
        swipe(sheetTop() + 260, 850, true);
        QTRY_VERIFY(!trackSheet->property("visible").toBool());
        QCOMPARE(core.nowPlaying().title(), track);

        // Glass is blurred anew as what's behind changes, only while it's there,
        // and once when the Developer tools setting turns that off.
        {
            auto* theme = engine.singletonInstance<QObject*>("Cloudmus.Mobile", "Theme");
            QVERIFY(theme);
            auto* snapshot = trackSheet->findChild<QObject*>("sheetSnapshot");
            QVERIFY(snapshot);
            QVERIFY(theme->setProperty("dynamicBlur", true));
            QCOMPARE(snapshot->property("live").toBool(), false);
            QVERIFY(QMetaObject::invokeMethod(trackSheet, "open"));
            QTRY_VERIFY(trackSheet->property("opened").toBool());
            QCOMPARE(snapshot->property("live").toBool(), true);
            QVERIFY(theme->setProperty("dynamicBlur", false));
            QCOMPARE(snapshot->property("live").toBool(), false);
            QVERIFY(QMetaObject::invokeMethod(trackSheet, "close"));
            QTRY_VERIFY(!trackSheet->property("visible").toBool());
        }

        // Over Now Playing, a swipe takes the sheet and leaves that where it is.
        QVERIFY(QMetaObject::invokeMethod(sheet, "open"));
        QTRY_COMPARE(progress(), 1.0);
        {
            // Sideways it changes the track, the cover going with the finger: not
            // for a short way, and it doesn't take Now Playing down.
            auto* cover = findItem(findItem(window->contentItem(), QStringLiteral("nowPlaying")), QStringLiteral("trackCover"));
            auto* panel = cover->parentItem();
            const auto sideways = [&](int fromX, int toX, bool release) {
                QTest::touchEvent(window, finger.get()).press(0, QPoint(fromX, 300), window);
                for (int i = 1; i <= 10; ++i) {
                    QTest::qWait(30);
                    QTest::touchEvent(window, finger.get()).move(0, QPoint(fromX + (toX - fromX) * i / 10, 300), window);
                }
                QTest::qWait(200);
                if (release)
                    QTest::touchEvent(window, finger.get()).release(0, QPoint(toX, 300), window);
            };
            // Begun sideways, a gesture stays that: going down from there doesn't
            // take the panel with it, and begun downwards it doesn't change the track.
            QTest::touchEvent(window, finger.get()).press(0, QPoint(300, 300), window);
            for (int i = 1; i <= 6; ++i) {
                QTest::qWait(30);
                QTest::touchEvent(window, finger.get()).move(0, QPoint(300 - i * 10, 300), window);
            }
            for (int i = 1; i <= 10; ++i) {
                QTest::qWait(30);
                QTest::touchEvent(window, finger.get()).move(0, QPoint(240, 300 + i * 30), window);
            }
            QCOMPARE(progress(), 1.0);
            QVERIFY(panel->property("_swipeX").toReal() < 0);
            QTest::qWait(150);
            QTest::touchEvent(window, finger.get()).release(0, QPoint(240, 600), window);
            QTRY_COMPARE(panel->property("_swipeX").toReal(), 0.0);
            QCOMPARE(progress(), 1.0);

            QTest::touchEvent(window, finger.get()).press(0, QPoint(300, 200), window);
            for (int i = 1; i <= 6; ++i) {
                QTest::qWait(30);
                QTest::touchEvent(window, finger.get()).move(0, QPoint(300, 200 + i * 10), window);
            }
            QTRY_VERIFY(progress() < 1.0);
            for (int i = 1; i <= 10; ++i) {
                QTest::qWait(30);
                QTest::touchEvent(window, finger.get()).move(0, QPoint(300 - i * 25, 260), window);
            }
            QCOMPARE(panel->property("_swipeX").toReal(), 0.0);
            QTest::qWait(150);
            QTest::touchEvent(window, finger.get()).release(0, QPoint(50, 260), window);
            QTRY_COMPARE(progress(), 1.0);

            // What's next and what's before it, for it to come in.
            core.activePlaylist().playRow(0);
            ViewModel::MainScreen& main = core.mainScreen();
            QTRY_VERIFY(main.canGoNext());
            QVERIFY(!main.canGoPrevious());
            QVERIFY(!main.nextSeed().isEmpty());
            const int playing = 0;
            QTRY_COMPARE(core.core().playback().currentIndex(), playing);

            // A short way: back, and the track stays.
            sideways(300, 260, false);
            QVERIFY(panel->property("_swipeX").toReal() < 0);
            auto* neighbor = findItem(cover, QStringLiteral("trackCoverNeighbor"));
            QVERIFY(neighbor);
            QVERIFY(neighbor->isVisible());
            QVERIFY(neighbor->x() > 0 && neighbor->x() < cover->width() + 40);
            // It's dissolving as it goes.
            QVERIFY(findItem(cover, QStringLiteral("trackCoverPicture"))->opacity() < 1);
            // Not cut off where it was, but not to be taken far either.
            QVERIFY(!cover->clip());
            QVERIFY(qAbs(panel->property("_swipeX").toReal()) < cover->width());
            // The neighbor only looks out: it comes in on the throw.
            QVERIFY(panel->property("_newNear").toReal() < 0.6);
            QTest::touchEvent(window, finger.get()).release(0, QPoint(260, 300), window);
            QTRY_COMPARE(panel->property("_swipeX").toReal(), 0.0);
            QTRY_VERIFY(!neighbor->isVisible());
            QCOMPARE(core.core().playback().currentIndex(), playing);

            // Nothing before the first: it only gives a little, and goes back.
            sideways(60, 360, false);
            QVERIFY(qAbs(panel->property("_swipeX").toReal()) < 60);
            QVERIFY(!neighbor->isVisible());
            QTest::touchEvent(window, finger.get()).release(0, QPoint(360, 300), window);
            QTRY_COMPARE(panel->property("_swipeX").toReal(), 0.0);
            QCOMPARE(core.core().playback().currentIndex(), playing);

            // All the way: the next track, and what's before it is known. What comes
            // in stays what it was as the track changes under it.
            QSignalSpy neighborChanged(neighbor, SIGNAL(seedChanged()));
            sideways(380, 40, false);
            const QString coming = neighbor->property("seed").toString();
            QVERIFY(!coming.isEmpty());
            QCOMPARE(core.mainScreen().nextSeed(), coming);
            neighborChanged.clear();
            QTest::touchEvent(window, finger.get()).release(0, QPoint(40, 300), window);
            QTRY_VERIFY(panel->property("_handingOver").toBool() || core.core().playback().currentIndex() != playing);
            QTRY_VERIFY(core.core().playback().currentIndex() != playing);
            if (neighbor->isVisible())
                QCOMPARE(neighbor->property("seed").toString(), coming);
            QTRY_VERIFY(core.core().playback().currentIndex() != playing);
            QTRY_COMPARE(panel->property("_swipeX").toReal(), 0.0);
            QTRY_VERIFY(!panel->property("_handingOver").toBool());
            QVERIFY(!neighbor->isVisible());
            // At most going away with the rest once it's over, never to another's.
            QVERIFY(neighborChanged.count() <= 1);
            QVERIFY(neighbor->property("seed").toString().isEmpty());
            QCOMPARE(findItem(cover, QStringLiteral("trackCoverPicture"))->opacity(), 1.0);
            QCOMPARE(progress(), 1.0);
            QTRY_VERIFY(main.canGoPrevious());
            sideways(40, 380, true);
            QTRY_COMPARE(core.core().playback().currentIndex(), playing);
            QTRY_COMPARE(panel->property("_swipeX").toReal(), 0.0);
        }
        {
            // With a finger on its own button.
            QQuickItem* menu = nullptr;
            forEachItem(findItem(window->contentItem(), QStringLiteral("nowPlaying")), [&](QQuickItem* item) {
                if (item->objectName() == QStringLiteral("nowPlayingMenu"))
                    menu = item;
            });
            QVERIFY(menu);
            QTRY_VERIFY(menu->isVisible());
            const QPoint at = menu->mapToScene(QPointF(menu->width() / 2, menu->height() / 2)).toPoint();
            QTest::touchEvent(window, finger.get()).press(0, at, window);
            QTest::qWait(30);
            QTest::touchEvent(window, finger.get()).release(0, at, window);
        }
        QTRY_VERIFY(trackSheet->property("opened").toBool());
        swipe(sheetTop() + 60, 850, false);
        QVERIFY(trackSheet->property("slide").toReal() > 0);
        QCOMPARE(progress(), 1.0);
        QTest::touchEvent(window, finger.get()).release(0, QPoint(300, 850), window);
        QTRY_VERIFY(!trackSheet->property("visible").toBool());
        QTest::qWait(400);
        QCOMPARE(progress(), 1.0);
        QVERIFY(QMetaObject::invokeMethod(sheet, "close"));
        QTRY_COMPARE(progress(), 0.0);

        // Playback going on brings the list to its row; a finger may take it
        // away again, and the next track brings it back.
        window->resize(320, 300); // still taller than wide enough to be a phone
        auto* list = findItem(window->contentItem(), QStringLiteral("mainList"));
        QVERIFY(list);
        QTRY_VERIFY(list->height() < list->property("contentHeight").toReal());
        core.activePlaylist().playRow(0);
        QTRY_COMPARE(list->property("contentY").toReal(), 0.0);
        core.activePlaylist().playRow(1);
        QTRY_VERIFY(list->property("contentY").toReal() > 0);
    }

    // On a head unit a swipe works all over the panel's side of the screen, in
    // either layout, also where the panel itself has moved away from: not on the
    // list beside it.
    void aSwipeWorksAllOverTheHeadUnitsPanel()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        window->resize(1280, 720);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        QTest::qWait(600);
        core.activePlaylist().playRow(1);
        QTRY_VERIFY(core.mainScreen().canGoPrevious());
        auto* panel = window->findChild<QQuickItem*>("trackPanel");
        auto* toggle = window->findChild<QQuickItem*>("screenModeToggle");
        QVERIFY(panel && toggle);
        std::unique_ptr<QPointingDevice> finger(QTest::createTouchDevice());
        const auto reaches = [&](int fromX, int y) {
            QTest::touchEvent(window, finger.get()).press(0, QPoint(fromX, y), window);
            for (int i = 1; i <= 8; ++i) {
                QTest::qWait(30);
                QTest::touchEvent(window, finger.get()).move(0, QPoint(fromX + i * 8, y), window);
            }
            const bool moved = panel->property("_swipeX").toReal() != 0;
            QTest::qWait(200);
            QTest::touchEvent(window, finger.get()).release(0, QPoint(fromX + 64, y), window);
            QTest::qWaitFor([&]() { return panel->property("_swipeX").toReal() == 0; }, 3000);
            return moved;
        };
        const int edge = 4;
        for (const bool expanded : { false, true }) {
            if (expanded) {
                QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
                QTRY_COMPARE(panel->property("progress").toReal(), 1.0);
                QTest::qWait(100);
            }
            // The very edge, away from the buttons up and down it; and over the panel.
            QVERIFY2(reaches(edge, 360), expanded ? "edge, expanded" : "edge, normal");
            // On the cover: the details beside it, the seek bar, take a drag themselves.
            auto* cover = window->findChild<QQuickItem*>("trackCover");
            QVERIFY(cover);
            QVERIFY2(reaches(int(cover->mapToScene(QPointF(cover->width() / 2, 0)).x()), 360),
                expanded ? "panel, expanded" : "panel, normal");
        }
        // Over the list it scrolls the list.
        QVERIFY(!reaches(window->width() - 40, 360));
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QTRY_COMPARE(panel->property("progress").toReal(), 0.0);
    }

    // A head unit's screen left turned to the track's comes up that way, not
    // unfolding from the other; the track's panel over it has a grip (as the
    // sheet on a phone has), and its glass follows what's behind unless told
    // not to.
    void anExpandedScreenStartsExpanded()
    {
        QSettings().setValue(QStringLiteral("ui/screenExpanded"), true);
        {
            QTemporaryDir dir;
            App::MobileCore core({ dir.filePath("core.ini") });
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
            auto* page = window->findChild<QQuickItem*>("playlist");
            QVERIFY(page);
            QList<qreal> drawn;
            connect(window, &QQuickWindow::frameSwapped, window,
                [&]() { drawn.append(page->property("screenProgress").toReal()); });
            window->resize(1280, 720);
            QVERIFY(QTest::qWaitForWindowExposed(window));
            QVERIFY(startScreenGone(window));
            QTest::qWait(800);
            QCOMPARE(page->property("screenProgress").toReal(), 1.0);
            for (const qreal progress : std::as_const(drawn))
                QVERIFY2(progress == 0.0 || progress == 1.0, qPrintable(QString::number(progress)));
            // Shuffle and repeat right of the title from the start, not over it.
            {
                auto* title = window->findChild<QQuickItem*>("trackTitle");
                QVERIFY(title);
                const QRectF titleAt = title->mapRectToScene(title->boundingRect());
                for (const char* name : { "shuffleButton", "repeatButton" }) {
                    auto* button = window->findChild<QQuickItem*>(QLatin1String(name));
                    QVERIFY2(button && button->isVisible(), name);
                    const QRectF at = button->mapRectToScene(button->boundingRect());
                    QVERIFY2(at.left() >= titleAt.left() + title->width() * title->scale() - 1,
                        qPrintable(QStringLiteral("%1 at %2, the title %3..%4").arg(name).arg(at.left())
                            .arg(titleAt.left()).arg(titleAt.left() + title->width() * title->scale())));
                    QVERIFY2(qAbs(at.center().y() - titleAt.center().y()) < titleAt.height(), name);
                }
            }
            // The list's menu, from the button at the left, clear of a system
            // bar there: not right-aligned with the button, off the screen.
            {
                auto* theme = engine.singletonInstance<QObject*>("Cloudmus.Mobile", "Theme");
                QVERIFY(theme->setProperty("insetLeft", 60));
                auto* menu = window->findChild<QObject*>("railDropMenu");
                QVERIFY(menu);
                QVERIFY(QMetaObject::invokeMethod(menu, "open"));
                QTRY_VERIFY(menu->property("opened").toBool());
                QVERIFY2(menu->property("x").toReal() >= 60, qPrintable(menu->property("x").toString()));
                QVERIFY(menu->property("x").toReal() + menu->property("width").toReal() <= 1280);
                // As wide as its labels from its first opening: none cut short.
                int labels = 0;
                for (int i = 0; i < menu->property("count").toInt(); ++i) {
                    QQuickItem* entry = nullptr;
                    QVERIFY(QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, entry), Q_ARG(int, i)));
                    if (!entry || !entry->isVisible())
                        continue;
                    forEachItem(entry, [&](QQuickItem* item) {
                        if (!item->property("truncated").isValid())
                            return;
                        ++labels;
                        QVERIFY2(!item->property("truncated").toBool(), qPrintable(item->property("text").toString()));
                    });
                }
                QVERIFY(labels > 0);
                QVERIFY(QMetaObject::invokeMethod(menu, "close"));
                QTRY_VERIFY(!menu->property("visible").toBool());
                QVERIFY(theme->setProperty("insetLeft", 0));
            }

            auto* trackSheet = window->findChild<QObject*>("trackSheet");
            QVERIFY(trackSheet);
            auto* handle = trackSheet->findChild<QQuickItem*>("sheetHandle");
            auto* snapshot = trackSheet->findChild<QObject*>("sheetSnapshot");
            QVERIFY(handle && snapshot);
            QVERIFY(QMetaObject::invokeMethod(trackSheet, "open"));
            QTRY_VERIFY(trackSheet->property("opened").toBool());
            QVERIFY(handle->isVisible());
            QCOMPARE(snapshot->property("live").toBool(), true);
            QVERIFY(handle->height() > handle->width());
            // Taken by the grip, the panel goes with the finger.
            std::unique_ptr<QPointingDevice> finger(QTest::createTouchDevice());
            const auto dragGrip = [&](QPoint by) {
                const QPoint from = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
                QTest::touchEvent(window, finger.get()).press(0, from, window);
                for (int i = 1; i <= 10; ++i) {
                    QTest::qWait(20);
                    QTest::touchEvent(window, finger.get()).move(0, from + by * i / 10, window);
                }
                QTest::qWait(20);
                const qreal slid = trackSheet->property("slide").toReal();
                QTest::touchEvent(window, finger.get()).release(0, from + by, window);
                return slid;
            };
            QVERIFY(dragGrip(QPoint(400, 0)) > 0.3);
            QTRY_VERIFY(!trackSheet->property("visible").toBool());
            // On a phone, where the sheet rises from the bottom, it lies across the top.
            window->resize(420, 860);
            QTRY_VERIFY(!trackSheet->property("wide").toBool());
            QVERIFY(QMetaObject::invokeMethod(trackSheet, "open"));
            QTRY_VERIFY(trackSheet->property("opened").toBool());
            QVERIFY(handle->isVisible());
            QVERIFY(handle->width() > handle->height());
            QTest::qWait(400);
            QVERIFY(dragGrip(QPoint(0, 400)) > 0.3);
            QTRY_VERIFY(!trackSheet->property("visible").toBool());
        }
        QSettings().remove(QStringLiteral("ui/screenExpanded"));
    }

    // Audio opened from another app: whatever is open closes, and its track
    // shows — Now Playing on a phone, the track's screen on a head unit.
    void openedAudioShowsItsTrack()
    {
        QSettings().remove(QStringLiteral("ui/screenExpanded"));
        // After the engine has gone: its Settings write the turned screen back
        // as they go, and the tests after this one would start on it.
        const auto unturn = qScopeGuard([] { QSettings().remove(QStringLiteral("ui/screenExpanded")); });
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
        Ui::Backend backend(core);
        Ui::Platform platform(dir.path());
        playFake(core);
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
        window->resize(420, 860);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        auto* stack = window->findChild<QQuickItem*>("stack");
        auto* sheet = window->findChild<QQuickItem*>("nowPlayingSheet");
        auto* trackSheet = window->findChild<QObject*>("trackSheet");
        auto* page = window->findChild<QQuickItem*>("playlist");
        QVERIFY(stack && sheet && trackSheet && page);
        QTRY_VERIFY(window->findChild<QQuickItem*>("nowPlaying") != nullptr);

        QVERIFY(QMetaObject::invokeMethod(page, "settingsRequested"));
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(trackSheet, "open"));
        QTRY_VERIFY(trackSheet->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(&platform, "audioOpened"));
        QTRY_VERIFY(!trackSheet->property("visible").toBool());
        QTRY_COMPARE(stack->property("depth").toInt(), 1);
        QTRY_COMPARE(sheet->property("progress").toReal(), 1.0);

        // A head unit: the track's screen, the hero grown beside the list.
        window->resize(1280, 720);
        QTRY_COMPARE(sheet->property("progress").toReal(), 0.0);
        QCOMPARE(page->property("screenProgress").toReal(), 0.0);
        QVERIFY(QMetaObject::invokeMethod(page, "libraryRequested"));
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(&platform, "audioOpened"));
        QTRY_COMPARE(stack->property("depth").toInt(), 1);
        QTRY_COMPARE(page->property("screenProgress").toReal(), 1.0);
    }

    // A source can take seconds to start the next track. Until then the cover
    // thrown in stays, rather than the old one coming back, and no animation of a
    // new cover plays once the track has come.
    // A swipe turned the other way, quickly, before the last has gone back: the
    // neighbor that looks out is the other one, never with the first one's
    // cover on it while its own comes.
    void aTurnedSwipeShowsOnlyItsOwnNeighbor()
    {
        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("cover"), new Ui::CoverImageProvider);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            Item {
                width: 400; height: 800
                property alias panel: thePanel
                QtObject {
                    id: now
                    property string title: "Now"
                    property string artist: ""
                    property string coverUrl: ""
                    property string backgroundSeed: "Now"
                    property bool hasTrack: true
                    property bool playing: true
                    property real positionMs: 0
                    property real durationMs: 1000
                    property real bufferedMs: -1
                    function next() {}
                    function previous() {}
                }
                QtObject {
                    id: around
                    objectName: "around"
                    property bool canGoNext: true
                    property bool canGoPrevious: true
                    property string nextCoverUrl: ""
                    property string nextSeed: "Next"
                    property string previousCoverUrl: ""
                    property string previousSeed: "Previous"
                }
                TrackPanel { id: thePanel; anchors.fill: parent; nowPlaying: now; neighbors: around }
            })", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* panel = root->property("panel").value<QObject*>();
        auto* neighbor = panel->findChild<QObject*>("trackCoverNeighbor");
        QVERIFY(neighbor);
        const auto shown = [&]() { return neighbor->property("shownSource").toString(); };
        const auto turn = [&](int to, const QString& own, const QString& other) {
            QVERIFY(panel->setProperty("_swipeTo", to));
            QVERIFY(panel->setProperty("_swipeX", -60.0 * to));
            QElapsedTimer watching;
            watching.start();
            while (!shown().endsWith(own) && watching.elapsed() < 3000) {
                QVERIFY2(!shown().endsWith(other), qPrintable(QStringLiteral("%1 in place of %2").arg(other, own)));
                QTest::qWait(5);
            }
            QVERIFY(shown().endsWith(own));
        };
        const QString next = QStringLiteral("/sNext");
        const QString previous = QStringLiteral("/sPrevious");
        // Left, then right; right, then left.
        turn(1, next, previous);
        turn(-1, previous, next);
        turn(1, next, previous);
        // Both ways one album: its cover comes back.
        auto* around = root->findChild<QObject*>(QStringLiteral("around"));
        QVERIFY(around);
        QVERIFY(around->setProperty("previousSeed", QStringLiteral("Next")));
        QVERIFY(panel->setProperty("_swipeTo", -1));
        QTRY_VERIFY(shown().endsWith(next));
    }

    void aThrownCoverWaitsForASlowTrack()
    {
        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("cover"), new Ui::CoverImageProvider);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            Item {
                id: root
                width: 400; height: 800
                property alias panel: thePanel
                QtObject {
                    id: now
                    property string title: "Old"
                    property string artist: ""
                    property string coverUrl: ""
                    property string backgroundSeed: "Old"
                    property bool hasTrack: true
                    property bool playing: true
                    property real positionMs: 0
                    property real durationMs: 1000
                    property real bufferedMs: -1
                    property int asked: 0
                    function next() { asked++ }
                    function previous() {}
                }
                property alias now: now
                QtObject {
                    id: around
                    property bool canGoNext: true
                    property bool canGoPrevious: false
                    property string nextCoverUrl: ""
                    property string nextSeed: "New"
                    property string previousCoverUrl: ""
                    property string previousSeed: ""
                }
                TrackPanel { id: thePanel; anchors.fill: parent; nowPlaying: now; neighbors: around }
            })", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* panel = root->property("panel").value<QObject*>();
        auto* now = root->property("now").value<QObject*>();
        QVERIFY(panel->setProperty("_swipeTo", 1));
        QVERIFY(panel->setProperty("_swipeX", -100.0));
        QVERIFY(QMetaObject::invokeMethod(panel, "_throw"));
        QTRY_VERIFY(panel->property("_handingOver").toBool());
        QCOMPARE(now->property("asked").toInt(), 1);

        // Longer than a cover ever gets: still waiting, not back on the old one.
        QTest::qWait(1800);
        QVERIFY(panel->property("_handingOver").toBool());
        QVERIFY(panel->property("_swipeX").toReal() != 0);

        QVERIFY(now->setProperty("title", QStringLiteral("New")));
        QVERIFY(now->setProperty("backgroundSeed", QStringLiteral("New")));
        QTRY_VERIFY_WITH_TIMEOUT(!panel->property("_handingOver").toBool(), 3000);
        QCOMPARE(panel->property("_swipeX").toReal(), 0.0);
    }

    // What's downloaded ahead, as on the desktop: a cell each, thinner if there
    // are more, one bar if there are many.
    void preloadCellsFitWhatsAhead()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            PreloadCells { width: implicitWidth; fractions: [] })", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* cells = qobject_cast<QQuickItem*>(root.get());
        const auto set = [&](int count, double fraction) {
            QVariantList list;
            for (int i = 0; i < count; ++i)
                list.append(fraction);
            QVERIFY(cells->setProperty("fractions", list));
        };
        QVERIFY(!cells->isVisible());
        QCOMPARE(cells->implicitWidth(), 0.0);
        set(3, 0.5);
        QCOMPARE(cells->property("shownCount").toInt(), 3);
        QVERIFY(cells->property("asCells").toBool());
        const qreal three = cells->implicitWidth();
        set(6, 0.5);
        const qreal most = cells->implicitWidth();
        QVERIFY(most > three);
        // However many, they take no more of the bar's width than this.
        set(7, 0.5);
        QVERIFY(cells->implicitWidth() <= most + 0.5);
        QVERIFY(cells->property("shownWidth").toReal() < cells->property("cellWidth").toReal());
        set(10, 0.5);
        QVERIFY(!cells->property("asCells").toBool());
        QVERIFY(!cells->property("asBar").toBool());
        QCOMPARE(cells->property("shownCount").toInt(), 10);
        QVERIFY(cells->implicitWidth() <= most + 0.5);
        set(50, 0.25);
        QVERIFY(cells->property("asBar").toBool());
        QCOMPARE(cells->property("shownCount").toInt(), 1);
        QVERIFY(cells->implicitWidth() <= most + 0.5);
        QVariant overall;
        QVERIFY(QMetaObject::invokeMethod(cells, "fractionOf", Q_RETURN_ARG(QVariant, overall), Q_ARG(QVariant, 0)));
        QCOMPARE(overall.toDouble(), 0.25);
    }

    // What plays is marked by bars that move, and they rest with the music.
    void thePlayingRowHasMovingBars()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            Item {
                width: 100; height: 100
                property alias shown: bars.visible
                EqualizerBars { id: bars; objectName: "bars" }
            })", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* bars = root->findChild<QQuickItem*>("bars");
        QVERIFY(bars);
        auto* bar = bars->findChild<QQuickItem*>("bar2");
        QVERIFY(bar);
        const qreal first = bar->height();
        QVERIFY(first > 0);
        QTRY_VERIFY(!qFuzzyCompare(bar->height(), first));
        QVERIFY(bar->height() <= bars->height() + 0.5);
    }

    // The mini player in the light theme: its background comes in, and the
    // text on it can be read.
    void miniPlayerInTheLightTheme()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
        Ui::Backend backend(core);
        Ui::Platform platform(dir.path());
        playFake(core);
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
        auto* theme = engine.singletonInstance<QObject*>("Cloudmus.Mobile", "Theme");
        QVERIFY(theme->setProperty("dark", false));
        window->resize(420, 860);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        // A bar over the bottom: the background goes under it, the rest not.
        QVERIFY(theme->setProperty("insetBottom", 40));
        QTRY_VERIFY(findItem(window->contentItem(), QStringLiteral("nowPlaying")) != nullptr);
        auto* mini = findItem(findItem(window->contentItem(), QStringLiteral("nowPlaying")), QStringLiteral("trackTitle"));
        QVERIFY(mini != nullptr);
        QTRY_VERIFY(mini->mapRectToScene(mini->boundingRect()).bottom() <= window->height() - 40);
        QTest::qWait(1500);
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        if (!shots.isEmpty())
            window->grabWindow().save(shots + QStringLiteral("/mini-light.png"));
        {
            // The ripple sets out from where the bar was touched.
            auto* ripple = findItem(window->contentItem(), QStringLiteral("miniRipple"));
            QVERIFY(ripple);
            // The wave is the page's, the bar is what of it is uncovered.
            auto* surface = findItem(window->contentItem(), QStringLiteral("nowPlayingSurface"));
            QVERIFY(surface);
            const QRectF bar = surface->mapRectToScene(surface->boundingRect());
            const QPoint at(int(bar.left()) + 300, int(bar.top()) + 20);
            QTest::mousePress(window, Qt::LeftButton, { }, at);
            QTest::qWait(250);
            // Made with the touch.
            auto* shader = ripple->findChild<QQuickItem*>("rippleShader");
            QVERIFY(shader);
            const QPointF origin = shader->property("origin").toPointF();
            const QPointF expected = ripple->mapFromScene(QPointF(at));
            QVERIFY2(qAbs(origin.x() - expected.x()) < 2 && qAbs(origin.y() - expected.y()) < 2,
                qPrintable(QStringLiteral("origin %1,%2 not at the touch %3,%4")
                               .arg(origin.x()).arg(origin.y()).arg(expected.x()).arg(expected.y())));
            QTest::mouseRelease(window, Qt::LeftButton, { }, at);
        }
        QVERIFY(theme->setProperty("insetBottom", 0));
        QVERIFY(theme->setProperty("dark", true));
    }

    // On a phone the mini player stays over the other pages too, which keep
    // clear of it; set so, it's the main page's only.
    void miniPlayerOverEveryPage()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        window->resize(420, 860);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        auto* theme = engine.singletonInstance<QObject*>("Cloudmus.Mobile", "Theme");
        auto* stack = window->findChild<QQuickItem*>("stack");
        QVERIFY(theme && stack);
        QTRY_VERIFY(window->findChild<QQuickItem*>("nowPlaying") != nullptr);
        auto* mini = window->findChild<QQuickItem*>("nowPlaying");
        QTRY_VERIFY(mini->isVisible());

        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "settingsRequested"));
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        QTRY_VERIFY(!stack->property("busy").toBool());
        auto* settings = window->findChild<QQuickItem*>("settings");
        QVERIFY(settings);
        QVERIFY(mini->isVisible());
        QCOMPARE(settings->property("bottomPadding").toReal(), theme->property("miniPlayerHeight").toReal());
        // Over the page, a tap on it still opens Now Playing.
        auto* sheet = window->findChild<QQuickItem*>("nowPlayingSheet");
        QVERIFY(sheet);
        QTest::mouseClick(window, Qt::LeftButton, { }, QPoint(200, 860 - 100));
        QTRY_COMPARE(sheet->property("progress").toReal(), 1.0);
        QVERIFY(QMetaObject::invokeMethod(sheet, "close"));
        QTRY_COMPARE(sheet->property("progress").toReal(), 0.0);

        // Off: gone from this page, the room with it, and back on the main one.
        auto* everywhere = window->findChild<QQuickItem*>("miniPlayerEverywhere");
        QVERIFY(everywhere);
        QVERIFY(QMetaObject::invokeMethod(everywhere, "toggled", Q_ARG(bool, false)));
        QTRY_VERIFY(!mini->isVisible());
        QCOMPARE(settings->property("bottomPadding").toReal(), theme->property("insetBottom").toReal());
        QVERIFY(QMetaObject::invokeMethod(stack, "close"));
        QTRY_COMPARE(stack->property("depth").toInt(), 1);
        QTRY_VERIFY(mini->isVisible());
        // As it was, for the tests after.
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "settingsRequested"));
        QTRY_VERIFY(window->findChild<QQuickItem*>("miniPlayerEverywhere") != nullptr);
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("miniPlayerEverywhere"), "toggled", Q_ARG(bool, true)));
    }

    // Scrolled down long content that then gets short (another source's
    // playlists): it's back in view, not scrolled past its end.
    void scrollAreaStaysWithinShorterContent()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            ScrollArea {
                width: 400; height: 600
                contentWidth: width
                contentHeight: 3000
            }
        )", QUrl());
        std::unique_ptr<QObject> area(component.create());
        QVERIFY2(area, qPrintable(component.errorString()));
        QVERIFY(area->setProperty("contentY", 2400));
        QVERIFY(area->setProperty("contentHeight", 400));
        QTRY_COMPARE(area->property("contentY").toReal(), 0.0);
        QVERIFY(area->setProperty("contentHeight", 3000));
        QVERIFY(area->setProperty("contentY", 2400));
        QVERIFY(area->setProperty("contentHeight", 1000));
        QTRY_COMPARE(area->property("contentY").toReal(), 400.0);
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

    // A dialog comes up from lower and smaller, and leaves the same way before
    // it's gone.
    void confirmDialogAnimates()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls.Basic
            import Cloudmus.Mobile
            ApplicationWindow {
                width: 400; height: 600; visible: true
                ConfirmDialog { objectName: "dialog" }
            }
        )", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QObject* dialog = root->findChild<QObject*>(QStringLiteral("dialog"));
        QVERIFY(dialog);

        QMetaObject::invokeMethod(dialog, "ask", Q_ARG(QVariant, QStringLiteral("Delete?")),
            Q_ARG(QVariant, QString()), Q_ARG(QVariant, QStringLiteral("Delete")));
        QVERIFY(dialog->property("visible").toBool());
        QVERIFY(dialog->property("rise").toReal() > 0.5);
        QVERIFY(dialog->property("scale").toReal() < 1.0);
        QTRY_COMPARE(dialog->property("rise").toReal(), 0.0);
        QTRY_COMPARE(dialog->property("scale").toReal(), 1.0);
        QTRY_COMPARE(dialog->property("opacity").toReal(), 1.0);

        QMetaObject::invokeMethod(dialog, "close");
        // Still there, on its way out.
        QVERIFY(dialog->property("visible").toBool());
        QTRY_VERIFY(dialog->property("rise").toReal() > 0.0);
        QTRY_COMPARE(dialog->property("visible").toBool(), false);
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
        // The radius asked for, in the picture's pixels: along the top edge it's
        // clear up to where the curve begins, solid past it.
        {
            const QImage image = provider.requestImage(QStringLiteral("32/sRoad"), nullptr, QSize(389, 389));
            int firstSolid = -1;
            for (int x = 0; x < image.width() && firstSolid < 0; ++x) {
                if (qAlpha(image.pixel(x, 0)) == 255)
                    firstSolid = x;
            }
            qInfo() << "first solid on the top edge:" << firstSolid;
            QVERIFY(firstSolid >= 26 && firstSolid <= 34);
        }
        QVERIFY(provider.requestImage(QStringLiteral("12/ffile%3A%2F%2F%2Fnone.png"), nullptr, QSize(64, 64)).isNull());
    }

    // What the source says and what the user does reach the row's roles, and
    // through a replica: the fake's liked list comes with `liked` set.
    void likesReachTheRows()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QTRY_VERIFY(core.core().playback().hasCurrentTrack());
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
        App::MobileCore core({ dir.filePath("core.ini") });
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);

        QSignalSpy messages(backend.mainScreen(), SIGNAL(message(int,QString,QString)));
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
        QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(QStringLiteral("fake")) != nullptr
                && core.core().sourceManager().client(QStringLiteral("fake"))->available(),
            10000);
        QMetaObject::invokeMethod(screen, "setEnabled", Q_ARG(bool, false));
        QTRY_COMPARE(screen->property("enabled").toBool(), false);
        QMetaObject::invokeMethod(screen, "setEnabled", Q_ARG(bool, true));
        QTRY_COMPARE(screen->property("enabled").toBool(), true);
        QVERIFY(!screen->property("directory").toString().isEmpty());

        // The subfolders, as on the desktop: the shared setting.
        using Layout = Config::Settings::DownloadLayout;
        QMetaObject::invokeMethod(screen, "setLayout", Q_ARG(int, int(Layout::ByArtistAlbum)));
        QTRY_COMPARE(screen->property("layout").toInt(), int(Layout::ByArtistAlbum));
        QCOMPARE(core.core().settings().downloadLayout(), Layout::ByArtistAlbum);
        QMetaObject::invokeMethod(screen, "setLayout", Q_ARG(int, 42));
        QCOMPARE(core.core().settings().downloadLayout(), Layout::ByArtistAlbum);
        QMetaObject::invokeMethod(screen, "setLayout", Q_ARG(int, int(Layout::Flat)));
        QTRY_COMPARE(screen->property("layout").toInt(), int(Layout::Flat));

        // A job's row, with what's under way in a line and in the notice.
        // Never ends by itself, in the fake: see FakeBackend.
        QAbstractItemModel* rows = backend.downloads();
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
        QTRY_COMPARE(screen->property("active").toBool(), true);
        QTRY_COMPARE(screen->property("jobCount").toInt(), 1);
        QTRY_VERIFY(screen->property("summary").toString().contains(QStringLiteral("Slow one")));
        const ViewModel::DownloadsScreen::Notice notice = core.downloadsScreen().notice();
        QVERIFY(notice.shown && notice.ongoing);
        QCOMPARE(notice.title, QStringLiteral("Slow one"));

        // The open playlist, waiting behind it: the header says so, and asking
        // again doesn't queue it twice; its row on the Downloads page stops it.
        QObject* playlist = backend.playlistScreen();
        QTRY_COMPARE(core.core().sources().playlists(QStringLiteral("fake")).size(), 2);
        QMetaObject::invokeMethod(
            playlist, "openPlaylist", Q_ARG(QString, QStringLiteral("fake")), Q_ARG(QString, QStringLiteral("p1")));
        QTRY_COMPARE(playlist->property("canDownload").toBool(), true);
        QCOMPARE(playlist->property("downloading").toBool(), false);
        QMetaObject::invokeMethod(playlist, "downloadAll");
        QTRY_COMPARE(playlist->property("downloading").toBool(), true);
        QMetaObject::invokeMethod(playlist, "downloadAll");
        QTRY_COMPARE(rows->rowCount(), 2);
        QTRY_VERIFY(screen->property("summary").toString().contains(QStringLiteral("1 more waiting")));
        QTRY_VERIFY(rows->data(rows->index(1, 0), roleOf(*rows, "jobId")).toInt() > 0);
        QMetaObject::invokeMethod(
            screen, "cancel", Q_ARG(int, rows->data(rows->index(1, 0), roleOf(*rows, "jobId")).toInt()));
        QTRY_COMPARE(playlist->property("downloading").toBool(), false);
        QCOMPARE(core.core().downloads().jobs().size(), 2);
        QCOMPARE(core.core().downloads().jobs().last().state, ViewModel::Downloads::State::Cancelled);

        // The main screen's button is the playing playlist's.
        QObject* main = backend.mainScreen();
        QMetaObject::invokeMethod(playlist, "playAll");
        QTRY_COMPARE(core.activePlaylist().context().playlist.id, QStringLiteral("p1"));
        QTRY_COMPARE(main->property("canDownloadPlaylist").toBool(), true);
        QCOMPARE(main->property("playlistDownloading").toBool(), false);
        QMetaObject::invokeMethod(main, "downloadPlaylist");
        QTRY_COMPARE(main->property("playlistDownloading").toBool(), true);
        QTRY_COMPARE(rows->rowCount(), 3);

        // Cancelling the rest from the page, and clearing it.
        QMetaObject::invokeMethod(screen, "cancelAll");
        QTRY_COMPARE(screen->property("active").toBool(), false);
        QTRY_COMPARE(rows->data(rows->index(0, 0), roleOf(*rows, "isCancelled")).toBool(), true);
        QTRY_COMPARE(screen->property("canClear").toBool(), true);
        // All cancelled: nothing for the notification to tell.
        QVERIFY(!core.downloadsScreen().notice().shown);
        QMetaObject::invokeMethod(screen, "clearFinished");
        QTRY_COMPARE(rows->rowCount(), 0);
        QTRY_COMPARE(screen->property("jobCount").toInt(), 0);
        QMetaObject::invokeMethod(screen, "setEnabled", Q_ARG(bool, false));
        QTRY_COMPARE(core.core().downloads().isEnabled(), false);
    }

    // The main list's filter, through a replica: a filtered row plays and shows
    // the right track. And the queue's modes from the buttons' slots.
    void mainFilterAndPlayModes()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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

    // The pages come up and switch over the real core;
    // CLOUDMUS_MOBILE_SHOTS=<dir> also saves each, at a phone's and a head
    // unit's size, to look at.
    // A phone turned on its side and back, with a track playing, the way the
    // sizes come on a real one.
    void turningThePhoneKeepsTheScreenAlive()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        window->resize(411, 838);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        QTest::qWait(300);
        auto* page = window->findChild<QQuickItem*>("playlist");
        QVERIFY(page);
        for (int turn = 0; turn < 4; ++turn) {
            window->resize(914, 387);
            QTest::qWait(500);
            QVERIFY(page->property("wide").toBool());
            window->resize(411, 838);
            QTest::qWait(500);
            QVERIFY(!page->property("wide").toBool());
        }
    }

    // Turned back to a phone's portrait after the track changed on its side: Now
    // Playing's cover is made anew, and comes at its full size.
    void aCoverAfterTurningIsSharp()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        window->resize(411, 838);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        const auto sharp = [&] {
            auto* nowPlaying = window->findChild<QQuickItem*>("nowPlaying");
            auto* picture = nowPlaying ? findItem(nowPlaying, QStringLiteral("trackCoverPicture")) : nullptr;
            if (!picture || !picture->property("_showing").toBool())
                return false;
            const int wanted = qRound(picture->width() * window->effectiveDevicePixelRatio());
            const int shown = picture->property("shownSide").toInt();
            qInfo() << "cover" << picture->width() << "shown at" << shown << "wanted" << wanted;
            return qAbs(shown - wanted) <= 3 && picture->width() > 200;
        };
        QTRY_VERIFY(sharp());
        window->resize(914, 387);
        QTest::qWait(500);
        const QString before = core.nowPlaying().title();
        core.activePlaylist().playRow(core.core().playback().currentIndex() == 0 ? 1 : 0);
        QTRY_VERIFY(core.nowPlaying().title() != before);
        QTest::qWait(500);
        window->resize(411, 838);
        QTest::qWait(1500);
        QVERIFY(sharp());
    }

    // A source that asked for a sign-in isn't started the next time: it shows,
    // asking for it, and comes up when opened. Once signed in, it starts again.
    void aSourceToSignInToWaitsUntilOpened()
    {
        qputenv("CLOUDMUS_FAKE_AUTH", "1");
        QTemporaryDir dir;
        const QString fake = QStringLiteral("fake");
        const QString settings = dir.filePath("core.ini");
        QSettings(settings, QSettings::IniFormat).setValue(QStringLiteral("unstartedSources"), QStringList { fake });
        {
            App::MobileCore core({ settings });
            QTest::qWait(300);
            QVERIFY(core.core().sourceManager().client(fake) == nullptr);
            QTRY_VERIFY(core.library().sourceRows().rowCount() >= 1);
            // Not running, and still called what it's called.
            QTRY_COMPARE(core.library().currentSourceName(), QStringLiteral("Fake Source"));
            QVERIFY(core.library().authNeeded());
            core.library().selectSource(fake);
            QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(fake) != nullptr
                    && core.core().sourceManager().client(fake)->available(),
                10000);
            QVERIFY(!core.core().sourceManager().isDeferred(fake));
            core.core().authStates().setAuthenticated(fake);
            QTRY_VERIFY(!QSettings(settings, QSettings::IniFormat)
                    .value(QStringLiteral("unstartedSources")).toStringList().contains(fake));
        }
        {
            App::MobileCore core({ settings });
            QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(fake) != nullptr, 10000);
        }
        qunsetenv("CLOUDMUS_FAKE_AUTH");
    }

    // The app was unloaded while the code a source showed was being confirmed
    // in a browser: while that code is good, the source starts at once to go
    // on with it. Once it's expired, the source waits as before.
    void aSourceSigningInStartsWhileItsCodeIsGood()
    {
        qputenv("CLOUDMUS_FAKE_AUTH", "1");
        QTemporaryDir dir;
        const QString fake = QStringLiteral("fake");
        const QString settings = dir.filePath("core.ini");
        const QString pendingKey = QStringLiteral("signInPendingUntil/") + fake;
        {
            QSettings store(settings, QSettings::IniFormat);
            store.setValue(QStringLiteral("unstartedSources"), QStringList { fake });
            store.setValue(pendingKey, QDateTime::currentDateTimeUtc().addSecs(300));
        }
        {
            App::MobileCore core({ settings });
            QVERIFY(!core.core().sourceManager().isDeferred(fake));
            QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(fake) != nullptr
                    && core.core().sourceManager().client(fake)->available(),
                10000);
            // Its code again: the Library is on it.
            core.core().authStates().setPrompt(fake, { codeMethod(QStringLiteral("AB"), 200) });
            QTRY_COMPARE(core.library().currentSourceId(), fake);
            // Kept alive while it waits (Android's SignInService) only with its
            // sign-in on screen, until it's over.
            QVERIFY(core.signInNotice().title.isEmpty());
            core.library().watchSignIn(fake);
            QVERIFY(!core.signInNotice().title.isEmpty());
            core.library().watchSignIn(QString());
            QVERIFY(core.signInNotice().title.isEmpty());
            core.library().watchSignIn(fake);
            QVERIFY2(core.signInNotice().title.contains(QStringLiteral("Fake Source")), qPrintable(core.signInNotice().title));
            QVERIFY(core.signInNotice().text.contains(QStringLiteral("AB")));
            QSignalSpy over(&core, &App::MobileCore::signInNoticeChanged);
            core.core().authStates().setAuthenticated(fake);
            QCOMPARE(over.count(), 1);
            QVERIFY(core.signInNotice().title.isEmpty());
        }
        {
            QSettings store(settings, QSettings::IniFormat);
            store.setValue(QStringLiteral("unstartedSources"), QStringList { fake });
            store.setValue(pendingKey, QDateTime::currentDateTimeUtc().addSecs(-1));
        }
        {
            App::MobileCore core({ settings });
            QVERIFY(core.core().sourceManager().isDeferred(fake));
            QTRY_VERIFY(!QSettings(settings, QSettings::IniFormat).contains(pendingKey));
        }
        qunsetenv("CLOUDMUS_FAKE_AUTH");
    }

    // The processes wait; what's known of them doesn't.
    void backendsStartAfterTheirAnnouncement()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini"), 300, 0 });
        const QString fake = QStringLiteral("fake");
        QVERIFY(core.core().sourceManager().client(fake) == nullptr);
        QTRY_VERIFY(core.library().sourceRows().rowCount() >= 1);
        QTRY_VERIFY_WITH_TIMEOUT(core.core().sourceManager().client(fake) != nullptr, 5000);
    }

    // Turning the screen over moves things and scales them, a GPU's work and
    // next to nothing. What must not happen on the way is what costs on a head
    // unit: text and its shadow laid out and drawn anew, textures of masks made
    // anew, a picture decoded or generated.
    void turningTheScreenOverRedrawsNothingHeavy_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("headunit") << QSize(1920, 1080);
        QTest::newRow("phone-landscape") << QSize(820, 362);
    }

    void turningTheScreenOverRedrawsNothingHeavy()
    {
        QFETCH(QSize, size);
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        window->resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        QTest::qWait(1200); // the first size settled, on everything

        auto* toggle = window->findChild<QQuickItem*>("screenModeToggle");
        auto* panel = window->findChild<QQuickItem*>("trackPanel");
        auto* title = window->findChild<QQuickItem*>("trackTitle");
        auto* picture = window->findChild<QQuickItem*>("trackCoverPicture");
        if (picture == nullptr)
            picture = window->findChild<QQuickItem*>("trackCover");
        auto* aura = window->findChild<QQuickItem*>("heroAura");
        auto* list = window->findChild<QQuickItem*>("mainList");
        QVERIFY(toggle && panel && title && picture && aura && list);

        QSignalSpy titleWidth(title, SIGNAL(widthChanged()));
        QSignalSpy titleFont(title, SIGNAL(titleSizeChanged()));
        QSignalSpy coverSize(picture, SIGNAL(widthChanged()));
        QSignalSpy coverPixels(picture, SIGNAL(_sideChanged()));
        QSignalSpy auraPixels(aura, SIGNAL(_pixelsChanged()));
        QSignalSpy listWidth(list, SIGNAL(widthChanged()));
        // The cover a swipe brings in: hidden, and its note's a picture to load anew
        // at each size.
        auto* neighbor = findItem(panel, QStringLiteral("trackCoverNeighbor"));
        QVERIFY(neighbor);
        QSignalSpy neighborWidth(neighbor, SIGNAL(widthChanged()));

        for (const qreal end : { 1.0, 0.0 }) {
            QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
            QTRY_COMPARE(panel->property("progress").toReal(), end);
            QTest::qWait(800); // what used to follow the end of the animation
        }
        qInfo().noquote() << QStringLiteral(
            "there and back: title width %1, title font %2, cover size %3, cover pixels %4, background pixels %5, "
            "list width %6").arg(titleWidth.size()).arg(titleFont.size()).arg(coverSize.size())
            .arg(coverPixels.size()).arg(auraPixels.size()).arg(listWidth.size());
        // Only at the ends for the title, which is laid out for where it arrives.
        QVERIFY(titleWidth.size() <= 6);
        QCOMPARE(titleFont.size(), 0); // it jumps otherwise, at the end of the way
        QCOMPARE(coverSize.size(), 0);
        QCOMPARE(coverPixels.size(), 0);
        QCOMPARE(auraPixels.size(), 0);
        QCOMPARE(listWidth.size(), 0);
        // At the ends as well: it was hundreds, one for each frame.
        QVERIFY(neighborWidth.size() <= 12);
    }

    // No network is a state to see, not an error.
    void offlineIsShownNotComplainedAbout()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QVERIFY(startScreenGone(window));
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

    // On the track's screen the bar has only the covers' width: what fades out
    // there (the offline badge, the update's and space's buttons) must narrow
    // away too, or it pushes the arrow back out past the edge and takes its taps.
    void theScreensArrowStaysWithTheBarsIcons()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        window->resize(1280, 720);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));

        core.core().connectivity().reportOffline(QStringLiteral("fake"));
        core.core().messages().offline(QStringLiteral("no network"));
        auto* badge = window->findChild<QQuickItem*>("offlineBadge");
        auto* toggle = window->findChild<QQuickItem*>("screenModeToggle");
        auto* panel = window->findChild<QQuickItem*>("trackPanel");
        QVERIFY(badge && toggle && panel);
        QTRY_VERIFY(badge->isVisible());

        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QTRY_COMPARE(panel->property("progress").toReal(), 1.0);
        const QRectF arrow = toggle->mapRectToScene(QRectF(0, 0, toggle->width(), toggle->height()));
        QVERIFY2(arrow.right() <= window->width() + 0.5,
                 qPrintable(QStringLiteral("arrow at %1..%2 of %3").arg(arrow.left()).arg(arrow.right())
                            .arg(window->width())));
        QVERIFY(!badge->isVisible());

        // A tap where it's drawn turns the screen back.
        QTest::mouseClick(window, Qt::LeftButton, {}, arrow.center().toPoint());
        QTRY_COMPARE(panel->property("progress").toReal(), 0.0);
    }

    // A phone on its side has no height for a cover above the title: it goes
    // beside it, clear of the source's button and of the text.
    void aLowPanelPutsTheCoverBesideTheTitle()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QVERIFY(startScreenGone(window));
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
        auto* shuffle = window->findChild<QQuickItem*>("shuffleButton");
        QVERIFY(shuffle && shuffle->isVisible());
        QVERIFY(!scene(shuffle).intersects(scene(bar)));
        QVERIFY(qAbs(scene(shuffle).center().y() - scene(bar).center().y()) <= 1);
        // The track's menu went up with them, to their right.
        auto* menu = window->findChild<QQuickItem*>("trackMenuButton");
        auto* repeat = window->findChild<QQuickItem*>("repeatButton");
        QVERIFY(menu && menu->isVisible() && repeat);
        QVERIFY(!scene(menu).intersects(scene(bar)));
        QVERIFY(!scene(menu).intersects(scene(title)));
        QVERIFY(qAbs(scene(menu).center().y() - scene(bar).center().y()) <= 1);
        QVERIFY(scene(menu).left() >= scene(repeat).right() - 1);
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

    // A source offering its browser first and a form: with no built-in browser
    // (not Android) it's the form, which keeps what's typed as the page
    // repaints, and submits it.
    void signingInByAFormKeepsWhatsTypedAndSubmitsIt()
    {
        qputenv("CLOUDMUS_FAKE_AUTH", "1");
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QVERIFY(startScreenGone(window));

        AuthMethod browser;
        browser.kind = QStringLiteral("browser");
        browser.url = QStringLiteral("https://example.com/sign-in");
        browser.cookieUrls = QList<QString> { QStringLiteral("https://example.com") };
        browser.doneCookies = QList<QString> { QStringLiteral("session") };
        AuthMethod form;
        form.kind = QStringLiteral("form");
        form.message = QStringLiteral("Paste the <code>headers</code>");
        form.fields = QList<AuthField> { AuthField { QStringLiteral("password"), std::nullopt, true, std::nullopt } };

        core.library().selectSource(fake);
        core.core().authStates().setPrompt(fake, { browser, form });
        QCOMPARE(core.library().authKinds(), (QStringList { QStringLiteral("browser"), QStringLiteral("form") }));
        QCOMPARE(core.library().authFormMessage(), QStringLiteral("Paste the headers"));
        QVERIFY(core.library().authBrowser().contains(QStringLiteral("https://example.com/sign-in")));
        // The browser connects as the source does.
        const auto connection = [&core] {
            return QJsonDocument::fromJson(core.library().authBrowser().toUtf8())
                .object()
                .value(QStringLiteral("connection"))
                .toObject();
        };
        QCOMPARE(connection().value(QStringLiteral("mode")).toString(), QStringLiteral("system"));
        QVERIFY(!core.library().authBrowserNeedsProxy());
        // The config file outlives a test, and a run.
        const auto forget = qScopeGuard([&core, &fake] {
            core.core().settings().setProxies({ });
            core.core().settings().setSourceConnection(fake, QLatin1String(Config::Settings::kSystemConnection));
        });
        core.core().settings().setProxies({ { QStringLiteral("home"), QStringLiteral("Home"),
            Config::ProxyConfig::Type::Socks5, QStringLiteral("10.0.0.2"), 1080, QStringLiteral("me"),
            QStringLiteral("secret") } });
        core.core().settings().setSourceConnection(fake, QStringLiteral("home"));
        QVERIFY(core.library().authBrowserNeedsProxy());
        QCOMPARE(connection(),
            (QJsonObject { { QStringLiteral("mode"), QStringLiteral("proxy") },
                { QStringLiteral("type"), QStringLiteral("socks5") }, { QStringLiteral("host"), QStringLiteral("10.0.0.2") },
                { QStringLiteral("port"), 1080 }, { QStringLiteral("username"), QStringLiteral("me") },
                { QStringLiteral("password"), QStringLiteral("secret") } }));
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "libraryRequested"));
        // The form's inputs are a Repeater's: in the item tree, not the objects'.
        const auto input = [window] {
            QQuickItem* found = nullptr;
            forEachItem(window->contentItem(), [&found](QQuickItem* item) {
                if (item->objectName() == QStringLiteral("authField_password"))
                    found = item;
            });
            return found;
        };
        QTRY_VERIFY(input() != nullptr);
        auto* field = input();
        QTRY_VERIFY(field->isVisible());
        QVERIFY(!window->findChild<QQuickItem*>("signIn")->isVisible());
        // Only one way here: nothing to choose.
        QVERIFY(!window->findChild<QQuickItem*>("authRecommended")->isVisible());

        field->setProperty("text", QStringLiteral("nope"));
        // The page repainting (a cover arriving) leaves the input alone.
        core.core().authStates().setPrompt(fake, { browser, form });
        QTest::qWait(100);
        QCOMPARE(input(), field);
        QCOMPARE(field->property("text").toString(), QStringLiteral("nope"));
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        if (!shots.isEmpty())
            window->grabWindow().save(shots + QStringLiteral("/phone-signin-form.png"));

        QSignalSpy refused(&core.core().sourcePage(), &ViewModel::SourcePage::submitFailed);
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("authSubmit"), "clicked"));
        QTRY_COMPARE(refused.count(), 1);
        QCOMPARE(refused.first().at(1).toString(), QStringLiteral("wrong password"));
    }

    // With no way to sign in yet, the card asks for them by itself and shows
    // no button meanwhile; if asking brings none, it can be asked again.
    void signingInAsksForTheWaysByItself()
    {
        qputenv("CLOUDMUS_FAKE_AUTH", "1");
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QVERIFY(startScreenGone(window));

        core.library().selectSource(fake);
        QVERIFY(!core.library().sourceStarting());
        core.core().authStates().setSignedOut(fake);
        QVERIFY(core.library().authPreparing());
        QVERIFY(core.library().watchedSignIn().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "libraryRequested"));
        // On screen while the Library is, and no longer.
        QTRY_COMPARE(core.library().watchedSignIn(), fake);
        // The fake says it's signed in, and sends no ways: nothing more is
        // coming.
        QTRY_VERIFY(!core.library().authPreparing());
        auto* button = window->findChild<QQuickItem*>("signIn");
        QTRY_VERIFY(button->isVisible());
        QCOMPARE(button->property("text").toString(), QStringLiteral("Try again"));
        QVERIFY(!window->findChild<QQuickItem*>("authPreparing")->isVisible());

        core.core().authStates().setPrompt(fake, { codeMethod(QStringLiteral("abc123")) });
        QTRY_VERIFY(!button->isVisible());
        QVERIFY(!window->findChild<QQuickItem*>("authPreparing")->isVisible());
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("library"), "closeRequested"));
        QTRY_VERIFY(core.library().watchedSignIn().isEmpty());
    }

    // With more than one way to carry out, they're offered, the source's
    // first one chosen; another can be picked.
    void signingInOffersTheWaysThereAre()
    {
        qputenv("CLOUDMUS_FAKE_AUTH", "1");
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QVERIFY(startScreenGone(window));

        AuthMethod form;
        form.kind = QStringLiteral("form");
        form.fields = QList<AuthField> { AuthField { QStringLiteral("token"), std::nullopt, false, std::nullopt } };

        core.library().selectSource(fake);
        core.core().authStates().setPrompt(fake, { codeMethod(QStringLiteral("abc123")), form });
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "libraryRequested"));
        const auto find = [window](const QString& name) {
            QQuickItem* found = nullptr;
            forEachItem(window->contentItem(), [&](QQuickItem* item) {
                if (item->objectName() == name)
                    found = item;
            });
            return found;
        };
        QTRY_VERIFY(find(QStringLiteral("authWay_form")) != nullptr);
        QTRY_VERIFY(find(QStringLiteral("authWay_code"))->isVisible());
        QVERIFY(find(QStringLiteral("authWay_code"))->property("checked").toBool());
        QVERIFY(find(QStringLiteral("authWay_code"))->property("text").toString().startsWith(QChar(0x2605)));
        QVERIFY(find(QStringLiteral("authCode"))->isVisible());
        QVERIFY(!find(QStringLiteral("authSubmit"))->isVisible());

        QVERIFY(QMetaObject::invokeMethod(find(QStringLiteral("authWay_form")), "clicked"));
        QTRY_VERIFY(find(QStringLiteral("authSubmit"))->isVisible());
        QVERIFY(find(QStringLiteral("authField_token"))->isVisible());
        QVERIFY(!find(QStringLiteral("authCode"))->isVisible());
        QVERIFY(!find(QStringLiteral("authQr"))->isVisible());
        QTest::qWait(400);
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        if (!shots.isEmpty())
            window->grabWindow().save(shots + QStringLiteral("/phone-signin-choice.png"));
    }

    // Signing in on a narrow screen: all of it fits, the code copies, and once
    // signed in there's a way out.
    void signingInFitsAPhoneAndCanBeUndone()
    {
        qputenv("CLOUDMUS_FAKE_AUTH", "1");
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        CountingQrProvider::draws = 0;
        CountingQrProvider::drawsOnUiThread = 0;
        engine.addImageProvider(QStringLiteral("qr"), new CountingQrProvider);
        engine.setInitialProperties({
            { "backend", QVariant::fromValue<QObject*>(&backend) },
            { "platform", QVariant::fromValue<QObject*>(&platform) },
        });
        engine.loadFromModule("Cloudmus.Mobile", "Main");
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(360, 780);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));

        core.library().selectSource(fake);
        core.core().authStates().setPrompt(fake, { codeMethod(QStringLiteral("abc123")) });
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
        QVERIFY(window->findChild<QQuickItem*>("copyAuthCode")->property("copied").toBool());

        // A tap takes the QR code over the whole screen, and another back.
        auto* qr = window->findChild<QQuickItem*>("authQr");
        auto* full = qr->findChild<QObject*>("qrFullScreen");
        QVERIFY(full);
        const QPoint onQr = qr->mapToScene(QPointF(qr->width() / 2, qr->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, { }, onQr);
        QTRY_VERIFY(full->property("opened").toBool());
        auto* large = full->findChild<QQuickItem*>("qrFullScreenImage");
        QVERIFY(large);
        QTRY_COMPARE(large->width(), window->width() - 32.0);
        QVERIFY(large->width() > qr->width());
        if (!shots.isEmpty())
            window->grabWindow().save(shots + QStringLiteral("/phone-signin-qr.png"));
        QTest::mouseClick(window, Qt::LeftButton, { }, QPoint(10, 10));
        QTRY_VERIFY(!full->property("visible").toBool());
        // Drawn once for the page and the screen, never holding the UI up.
        QCOMPARE(CountingQrProvider::draws.load(), 1);
        QCOMPARE(CountingQrProvider::drawsOnUiThread.load(), 0);
        // Open again: signing in takes it away.
        QTest::mouseClick(window, Qt::LeftButton, { }, onQr);
        QTRY_VERIFY(full->property("opened").toBool());

        // Signing out is an entry of the menu in the title, there once signed in.
        auto* signOut = window->findChild<QQuickItem*>("signOut");
        QVERIFY(signOut);
        QVERIFY(!signOut->property("available").toBool());
        // Not signed in: what the source can do is listed; signed in, it isn't.
        QVERIFY(window->findChild<QQuickItem*>("capabilities")->isVisible());
        core.core().authStates().setAuthenticated(fake);
        QTRY_VERIFY(signOut->property("available").toBool());
        QTRY_VERIFY(!window->findChild<QQuickItem*>("capabilities")->isVisible());
        QTRY_VERIFY(!full->property("visible").toBool());
        // The menu: a tap beside it closes it and lands nowhere else.
        auto* menu = window->findChild<QObject*>("sourceMenu");
        QVERIFY(menu);
        {
            // Held as a finger holds it: the button shrinks, then grows back as the menu opens.
            const QPoint at = window->findChild<QQuickItem*>("sourceMenuButton")->mapToScene(QPointF(10, 10)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, { }, at);
            QTest::qWait(200);
            QTest::mouseRelease(window, Qt::LeftButton, { }, at);
        }
        // Its size is known from the first frame: a menu growing as it opens
        // shows as a jump. Signing out came while it was closed.
        QTRY_VERIFY(menu->property("visible").toBool());
        const qreal firstHeight = menu->property("height").toReal();
        QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("height").toReal(), firstHeight);
        QCOMPARE(firstHeight, 3 * window->findChild<QQuickItem*>("signOut")->height()
                + menu->property("topPadding").toReal() + menu->property("bottomPadding").toReal());
        for (int frame = 0; frame < 6; ++frame) {
            if (!shots.isEmpty())
                window->grabWindow().save(shots + QStringLiteral("/menu-%1.png").arg(frame));
            QTest::qWait(60);
        }
        auto* refreshEntry = window->findChild<QQuickItem*>("refreshEntry");
        QVERIFY(refreshEntry->isVisible());
        QCOMPARE(refreshEntry->opacity(), 1.0);
        // Beside it, on the button under the overlay: it doesn't get the tap.
        QSignalSpy outside(window->findChild<QObject*>("sourceMenuButton"), SIGNAL(clicked()));
        QTest::mouseClick(window, Qt::LeftButton, { }, window->findChild<QQuickItem*>("sourceMenuButton")
            ->mapToScene(QPointF(10, 10)).toPoint());
        QTRY_VERIFY(!menu->property("visible").toBool());
        QCOMPARE(outside.count(), 0);
        QTest::mouseClick(window, Qt::LeftButton, { }, window->findChild<QQuickItem*>("sourceMenuButton")
            ->mapToScene(QPointF(10, 10)).toPoint());
        QTRY_VERIFY(menu->property("opened").toBool());
        if (!shots.isEmpty()) {
            // The ripple, held: a finger down on an entry, then off it to cancel.
            const QPoint at = refreshEntry->mapToScene(QPointF(refreshEntry->width() * 0.3, refreshEntry->height() / 2)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, { }, at);
            int waited = 0;
            for (const int ms : { 120, 300, 700 }) {
                QTest::qWait(ms - waited);
                waited = ms;
                window->grabWindow().save(shots + QStringLiteral("/ripple-%1.png").arg(ms));
            }
            QTest::mouseRelease(window, Qt::LeftButton, { }, QPoint(5, window->height() - 5));
            QTRY_VERIFY(menu->property("opened").toBool());
        }
        {
            // The ripple's sizes follow the interface scale: at 1.5 the soft
            // edge and the grain are half again as large, and the menu still
            // lines up with its button.
            auto* theme = engine.singletonInstance<QObject*>("Cloudmus.Mobile", "Theme");
            QVERIFY(theme);
            theme->setProperty("scale", 1.5);
            QTest::qWait(300);
            auto* button = window->findChild<QQuickItem*>("sourceMenuButton");
            auto* entry = window->findChild<QQuickItem*>("sourceSettingsEntry");
            QVERIFY(menu->property("opened").toBool());
            const QPoint at = entry->mapToScene(QPointF(entry->width() * 0.3, entry->height() / 2)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, { }, at);
            QTest::qWait(400);
            auto* shader = entry->findChild<QQuickItem*>("rippleShader");
            QVERIFY(shader);
            QCOMPARE(shader->property("dp").toReal(), 1.5);
            if (!shots.isEmpty())
                window->grabWindow().save(shots + QStringLiteral("/ripple-scale15.png"));
            QTest::mouseRelease(window, Qt::LeftButton, { }, QPoint(5, window->height() - 5));
            QTRY_VERIFY(menu->property("opened").toBool());
            Q_UNUSED(button)
            theme->setProperty("scale", 1.0);
            QTest::qWait(300);
        }
        QVERIFY(QMetaObject::invokeMethod(signOut, "triggered"));
        QTRY_VERIFY(core.library().authNeeded());
        QTRY_VERIFY(!signOut->property("available").toBool());
    }

    // A folder's subfolder opens as a page of its own over the folder's, which
    // keeps what it showed; Back takes it off again. Through a replica, as on
    // Android, where a deeper level's screen is asked for on the way.
    void aSubfolderOpensAsAPage()
    {
        qputenv("CLOUDMUS_FAKE_SUBFOLDER", "1");
        const auto unset = qScopeGuard([] { qunsetenv("CLOUDMUS_FAKE_SUBFOLDER"); });
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        Ui::Platform platform(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(core.core().sources().playlists(QStringLiteral("fake")).size() == 3, 10000);

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
        window->resize(420, 860);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        auto* stack = window->findChild<QQuickItem*>("stack");
        QVERIFY(stack);

        // The folder, opened as the Library does.
        QMetaObject::invokeMethod(backend.playlistScreen(), "openPlaylist", Q_ARG(QString, QStringLiteral("fake")),
            Q_ARG(QString, QStringLiteral("p1")));
        QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "libraryRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        QMetaObject::invokeMethod(window->findChild<QQuickItem*>("library"), "playlistRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 3);
        auto* parentPage = window->findChild<QQuickItem*>("playlistScreen");
        QVERIFY(parentPage);
        QTRY_COMPARE(backend.openTracks()->rowCount(), 2);
        QQuickItem* folder = nullptr;
        QTRY_COMPARE(backend.openFolders()->rowCount(), 1);
        QTRY_VERIFY((folder = findItem(parentPage, QStringLiteral("folder_p3"))) != nullptr);

        // A filter matching neither a subfolder nor a track says so.
        auto* filter = findItem(parentPage, QStringLiteral("filter"));
        auto* nothingFound = findItem(parentPage, QStringLiteral("nothingFound"));
        QVERIFY(filter && nothingFound && !nothingFound->isVisible());
        filter->setProperty("text", QStringLiteral("no such track"));
        QTRY_COMPARE(nothingFound->opacity(), 1.0);
        QVERIFY(nothingFound->isVisible());
        filter->setProperty("text", QString());
        QTRY_VERIFY(!nothingFound->isVisible());
        QTRY_VERIFY((folder = findItem(parentPage, QStringLiteral("folder_p3"))) != nullptr);

        QMetaObject::invokeMethod(folder, "clicked");
        QTRY_COMPARE(stack->property("depth").toInt(), 4);
        QQuickItem* childPage = nullptr;
        QTRY_VERIFY((childPage = window->findChild<QQuickItem*>("playlistScreen1")) != nullptr);
        QTRY_VERIFY(childPage->property("screen").value<QObject*>() != nullptr);
        QTRY_COMPARE(childPage->property("screen").value<QObject*>()->property("title").toString(),
            QStringLiteral("Inside"));
        // The one under it shows what it did.
        QCOMPARE(parentPage->property("screen").value<QObject*>()->property("title").toString(),
            QStringLiteral("First"));
        QCOMPARE(core.playlistScreens().at(0).title(), QStringLiteral("First"));
        QCOMPARE(backend.openTracks()->rowCount(), 2);

        // Back: the subfolder's level lets go once its page is off the stack.
        QMetaObject::invokeMethod(stack, "close");
        QTRY_COMPARE(stack->property("depth").toInt(), 3);
        QTRY_VERIFY(core.playlistScreens().at(1).title().isEmpty());
        QCOMPARE(core.playlistScreens().at(0).title(), QStringLiteral("First"));
    }

    // A folder with hundreds of subfolders: they and its tracks are one lazy
    // list, so the page builds only the rows in view (a Repeater built every
    // one at once, and the page hitched as it opened).
    void manySubfoldersAreBuiltLazily()
    {
        qputenv("CLOUDMUS_FAKE_SUBFOLDER", "many");
        const auto unset = qScopeGuard([] { qunsetenv("CLOUDMUS_FAKE_SUBFOLDER"); });
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        Ui::Platform platform(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(core.core().sources().playlists(QStringLiteral("fake")).size() == 302, 10000);

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
        window->resize(420, 860);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        auto* stack = window->findChild<QQuickItem*>("stack");

        QMetaObject::invokeMethod(backend.playlistScreen(), "openPlaylist", Q_ARG(QString, QStringLiteral("fake")),
            Q_ARG(QString, QStringLiteral("p1")));
        QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "libraryRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        QMetaObject::invokeMethod(window->findChild<QQuickItem*>("library"), "playlistRequested");
        QTRY_COMPARE(stack->property("depth").toInt(), 3);
        auto* page = window->findChild<QQuickItem*>("playlistScreen");
        QVERIFY(page);
        auto* list = findItem(page, QStringLiteral("playlistTracks"));
        QVERIFY(list);
        // All of it in the list: 300 folders and 2 tracks.
        QTRY_COMPARE(list->property("count").toInt(), 302);
        QCOMPARE(page->property("folderCount").toInt(), 300);
        // A folder's row and a track's, by the roles of the one list.
        QAbstractItemModel* entries = backend.openEntries();
        QTRY_COMPARE(entries->data(entries->index(0, 0), roleOf(*entries, "isFolder")).toBool(), true);
        QTRY_COMPARE(entries->data(entries->index(0, 0), roleOf(*entries, "title")).toString(), QStringLiteral("Inside"));
        QTRY_COMPARE(entries->data(entries->index(300, 0), roleOf(*entries, "isFolder")).toBool(), false);
        QTRY_COMPARE(entries->data(entries->index(300, 0), roleOf(*entries, "title")).toString(), QStringLiteral("Track t1"));
        QTest::qWait(500);
        int built = 0;
        std::function<void(QQuickItem*)> count = [&](QQuickItem* item) {
            if (item->objectName().startsWith(QLatin1String("folder_")))
                ++built;
            for (QQuickItem* child : item->childItems())
                count(child);
        };
        count(list);
        QVERIFY2(built > 0 && built < 40, qPrintable(QStringLiteral("%1 folder rows built").arg(built)));

        // Scrolled to the end: the tracks, after the folders, play the right ones.
        QMetaObject::invokeMethod(list, "positionViewAtEnd");
        QTest::qWait(300);
        QMetaObject::invokeMethod(backend.playlistScreen(), "playRow", Q_ARG(int, 1));
        QTRY_VERIFY(core.core().playback().hasCurrentTrack());
        QTRY_COMPARE(core.nowPlaying().track().id, QStringLiteral("t2"));
    }

    // The pulse the views move with: the music is analyzed while any view
    // watches, saying so twice holds it once, and the level comes through.
    void pulseRunsWhileWatched()
    {
        QVector<bool> switches;
        double nextPts = 0;
        ViewModel::AudioPulse pulse({ [&switches](bool on) { switches.append(on); },
            [&nextPts]() {
                const QVector<Playback::LevelReading> due { { nextPts, -8, -30 } };
                nextPts += 0.023;
                return due;
            } });
        ViewModel::LogoSound logo({ []() { }, []() { return -1.0; } });
        ViewModel::PulseScreen screen(pulse, logo);
        QSignalSpy levels(&screen, &ViewModel::PulseScreen::levelChanged);
        screen.setWatching(true);
        screen.setWatching(true);
        QVERIFY(pulse.isActive());
        QTRY_VERIFY(screen.level() > 0.5f);
        // Every frame of analysis, but not as many to the views.
        QTest::qWait(500);
        QVERIFY2(levels.count() <= 20, qPrintable(QString::number(levels.count())));
        screen.setWatching(false);
        QVERIFY(!pulse.isActive());
        QCOMPARE(screen.level(), 0.0f);
        QCOMPARE(switches, (QVector<bool> { true, false }));
    }

    // Music made up where it can't be analyzed (Android): beats at a tempo the
    // detector finds, from playing only, and a level that's there.
    void imitatedMusicBeats()
    {
        // Paused: nothing to hear.
        bool playing = false;
        ViewModel::ImitatedMusic paused([&playing]() { return playing; }, 42);
        ViewModel::AudioPulse pulse(paused.source());
        pulse.acquire();
        QTest::qWait(100);
        pulse.poll(100);
        QCOMPARE(pulse.level(), 0.0f);
        pulse.release();

        // 20 s of it, stepped by hand.
        ViewModel::ImitatedMusic music([]() { return true; }, 42);
        Playback::BeatDetector detector;
        int beats = 0;
        float highest = 0;
        for (double t = 0; t < 20; t += 0.016) {
            for (const Playback::LevelReading& reading : music.advance(0.016)) {
                const auto result = detector.feed(reading);
                highest = std::max(highest, result.level);
                if (result.beat)
                    ++beats;
            }
        }
        // 96..132 BPM over 20 s, a break aside: some 25..45 beats.
        QVERIFY2(beats >= 20 && beats <= 50, qPrintable(QString::number(beats)));
        QVERIFY(highest > 0.5f);
    }

    // In another process: the views' holds come to the core as one, and its
    // beats come back.
    void pulseThroughAReplica()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
        Remote::Host host(core, dir.filePath("core.sock"));
        Ui::Backend backend(dir.filePath("core.sock"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 5000);

        backend.holdPulse();
        backend.holdPulse();
        QTRY_VERIFY(core.pulseScreen().watching());
        QVERIFY(core.core().audioPulse().isActive());
        QTRY_COMPARE(backend.pulse()->property("watching").toBool(), true);

        Receiver beat;
        QVERIFY(QObject::connect(backend.pulse(), SIGNAL(beat(float)), &beat, SLOT(run())));
        QSignalSpy beats(&beat, &Receiver::ran);
        emit core.pulseScreen().beat(0.8f);
        QTRY_COMPARE(beats.count(), 1);
        // The logo's sound's strikes come the same way. (Not played here: it
        // would sound on the machine running the tests.)
        Receiver strike;
        QVERIFY(QObject::connect(backend.pulse(), SIGNAL(logoHit(float)), &strike, SLOT(run())));
        QSignalSpy strikes(&strike, &Receiver::ran);
        emit core.core().logoSound().hit(1.0f);
        QTRY_COMPARE(strikes.count(), 1);

        backend.releasePulse();
        QTest::qWait(200);
        QVERIFY(core.pulseScreen().watching()); // another view holds it
        backend.releasePulse();
        QTRY_VERIFY(!core.pulseScreen().watching());
        QVERIFY(!core.core().audioPulse().isActive());
    }

    void onboardingOnTheFirstLaunch_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("phone") << QSize(420, 860);
        QTest::newRow("sideways") << QSize(915, 412);
        QTest::newRow("headunit") << QSize(1920, 1080);
    }

    // The first launch shows the slides once the start screen has gone; their
    // last one leads to the Library, and they don't come back.
    void onboardingOnTheFirstLaunch()
    {
        QFETCH(QSize, size);
        QSettings().remove(QStringLiteral("ui/onboardingSeen"));
        const auto restore = qScopeGuard([] { QSettings().setValue(QStringLiteral("ui/onboardingSeen"), true); });
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QCOMPARE(engine.rootObjects().size(), 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        auto* stack = window->findChild<QQuickItem*>("stack");
        QVERIFY(stack);
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        auto shot = [&](const QString& name) {
            QTest::qWait(900);
            if (!shots.isEmpty()) {
                QDir().mkpath(shots);
                window->grabWindow().save(QStringLiteral("%1/%2-%3.png").arg(shots, QTest::currentDataTag(), name));
            }
        };

        QTRY_VERIFY(window->findChild<QQuickItem*>("onboarding") != nullptr);
        auto* onboarding = window->findChild<QQuickItem*>("onboarding");
        QTRY_COMPARE(onboarding->opacity(), 1.0);
        auto* slides = findItem(onboarding, QStringLiteral("onboardingSlides"));
        auto* next = findItem(onboarding, QStringLiteral("onboardingNext"));
        QVERIFY(slides && next);
        const int count = slides->property("count").toInt();
        QCOMPARE(count, 5);
        for (int i = 0; i < count; ++i) {
            QTRY_COMPARE(onboarding->property("current").toInt(), i);
            auto* settling = onboarding->findChild<QObject*>("onboardingSettling");
            QVERIFY(settling);
            QTRY_VERIFY(!settling->property("running").toBool());
            QCOMPARE(slides->property("contentX").toReal(), i * slides->width());
            // Every word within the screen.
            forEachItem(onboarding, [&](QQuickItem* item) {
                if (item->objectName() != QLatin1String("onboardingTitle") || !item->isVisible())
                    return;
                const QRectF rect = item->mapRectToScene(item->boundingRect());
                if (rect.right() < 0 || rect.left() > window->width())
                    return; // another slide's
                QVERIFY2(rect.left() >= 0 && rect.right() <= window->width() + 0.5, qPrintable(QString::number(i)));
                QVERIFY(rect.bottom() <= window->height());
            });
            shot(QStringLiteral("onboarding-%1").arg(i));
            if (i < count - 1)
                QVERIFY(QMetaObject::invokeMethod(next, "clicked"));
        }

        // A finger's swipe: the slides follow it, smoothly, and go one slide on.
        {
            std::unique_ptr<QPointingDevice> finger(QTest::createTouchDevice());
            const int y = window->height() / 2;
            // From x by dx, in steps; the view's positions on the way.
            const auto swipe = [&](int x, int dx) {
                QList<qreal> path;
                QTest::touchEvent(window, finger.get()).press(0, QPoint(x, y), window);
                for (int i = 1; i <= 12; ++i) {
                    QTest::qWait(16);
                    QTest::touchEvent(window, finger.get()).move(0, QPoint(x + dx * i / 12, y), window);
                    path.append(slides->property("contentX").toReal());
                }
                QTest::touchEvent(window, finger.get()).release(0, QPoint(x + dx, y), window);
                return path;
            };
            const auto settled = [&](int index) {
                QTRY_COMPARE(onboarding->property("current").toInt(), index);
                auto* settling = onboarding->findChild<QObject*>("onboardingSettling");
                QTRY_VERIFY(!settling->property("running").toBool());
                QCOMPARE(slides->property("contentX").toReal(), index * slides->width());
            };
            const int width = window->width();
            // Back from the last one: one slide back, the view only ever going back.
            QList<qreal> path = swipe(width / 5, width / 2);
            settled(count - 2);
            for (int i = 1; i < path.size(); ++i)
                QVERIFY2(path[i] <= path[i - 1], qPrintable(QStringLiteral("%1 then %2").arg(path[i - 1]).arg(path[i])));
            QVERIFY(path.last() < (count - 1) * slides->width());
            // Pulled past the first: it gives, only that way, and comes back.
            QVERIFY(QMetaObject::invokeMethod(onboarding, "goTo", Q_ARG(QVariant, 0)));
            settled(0);
            path = swipe(width / 5, width / 2);
            settled(0);
            for (int i = 1; i < path.size(); ++i)
                QVERIFY2(path[i] <= path[i - 1], qPrintable(QStringLiteral("%1 then %2").arg(path[i - 1]).arg(path[i])));
            QVERIFY(path.last() < 0);
            // And on, to the last again.
            QVERIFY(QMetaObject::invokeMethod(onboarding, "goTo", Q_ARG(QVariant, count - 1)));
            settled(count - 1);
        }

        // The screen turned: the slide in view stays in view, and going back
        // goes one slide back.
        window->resize(size.transposed());
        QTRY_COMPARE(slides->width(), qreal(size.height()));
        QTRY_COMPARE(slides->property("contentX").toReal(), (count - 1) * slides->width());
        QVERIFY(QMetaObject::invokeMethod(onboarding, "goTo", Q_ARG(QVariant, count - 2)));
        QTRY_COMPARE(slides->property("contentX").toReal(), (count - 2) * slides->width());
        window->resize(size);
        QTRY_COMPARE(slides->width(), qreal(size.width()));
        QTRY_COMPARE(slides->property("contentX").toReal(), (count - 2) * slides->width());
        QVERIFY(QMetaObject::invokeMethod(onboarding, "goTo", Q_ARG(QVariant, count - 1)));
        QTRY_COMPARE(onboarding->property("current").toInt(), count - 1);
        QTRY_COMPARE(slides->property("contentX").toReal(), (count - 1) * slides->width());

        // The last slide's button: the Library, under the slides as they go.
        QVERIFY(QMetaObject::invokeMethod(next, "clicked"));
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        QTRY_VERIFY(window->findChild<QQuickItem*>("onboarding") == nullptr);
        // Settings writes it out a moment later.
        QTRY_VERIFY(QSettings().value(QStringLiteral("ui/onboardingSeen")).toBool());

        QVERIFY(QMetaObject::invokeMethod(stack, "close"));
        QTRY_COMPARE(stack->property("depth").toInt(), 1);
        QTRY_VERIFY(!stack->property("busy").toBool());

        // Asked for again from the menu: Skip closes them.
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "introRequested"));
        QTRY_VERIFY(window->findChild<QQuickItem*>("onboarding") != nullptr);
        onboarding = window->findChild<QQuickItem*>("onboarding");
        auto* skip = findItem(onboarding, QStringLiteral("onboardingSkip"));
        QVERIFY(skip);
        QVERIFY(QMetaObject::invokeMethod(skip, "clicked"));
        QTRY_VERIFY(window->findChild<QQuickItem*>("onboarding") == nullptr);
        QCOMPARE(stack->property("depth").toInt(), 1);
    }

    // The main page with nothing to play: the clouds fly, the music is watched
    // while it's seen, and its button leads to the Library.
    void welcomePanelLeadsToTheLibrary()
    {
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
        Ui::Backend backend(core);
        QTRY_VERIFY(backend.ready());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cloudmus.Mobile
            WelcomePanel { width: 420; height: 700 }
        )",
            QUrl());
        std::unique_ptr<QObject> object(
            component.createWithInitialProperties({ { "backend", QVariant::fromValue<QObject*>(&backend) } }));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* panel = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(panel);
        QTRY_VERIFY(core.pulseScreen().watching());
        auto* backdrop = panel->findChild<QQuickItem*>("welcomeClouds");
        QVERIFY(backdrop);
        const qreal before = backdrop->property("_time").toReal();
        QTRY_VERIFY(backdrop->property("_time").toReal() > before + 0.1);

        QSignalSpy library(panel, SIGNAL(libraryRequested()));
        auto* button = panel->findChild<QQuickItem*>("welcomeLibraryButton");
        QVERIFY(button);
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QCOMPARE(library.count(), 1);

        // Out of sight, nothing watched.
        panel->setVisible(false);
        QTRY_VERIFY(!core.pulseScreen().watching());
    }

    void aboutPageFromTheLibrary_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("phone") << QSize(420, 860);
        QTest::newRow("headunit") << QSize(1920, 1080);
    }

    // The Library's column leads to About: the version, the license, the
    // clouds flying, and the music watched only while the page is up.
    void aboutPageFromTheLibrary()
    {
        QFETCH(QSize, size);
        QTemporaryDir dir;
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QCOMPARE(engine.rootObjects().size(), 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(startScreenGone(window));
        auto* stack = window->findChild<QQuickItem*>("stack");
        QVERIFY(stack);
        const QString shots = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS");
        auto shot = [&](const char* name) {
            QTest::qWait(700);
            if (!shots.isEmpty()) {
                QDir().mkpath(shots);
                window->grabWindow().save(QStringLiteral("%1/%2-%3.png").arg(shots, QTest::currentDataTag(), name));
            }
        };

        QVERIFY(QMetaObject::invokeMethod(window->findChild<QQuickItem*>("playlist"), "libraryRequested"));
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        QTRY_VERIFY(!stack->property("busy").toBool());
        auto* aboutButton = window->findChild<QQuickItem*>("aboutButton");
        QVERIFY(aboutButton && aboutButton->isVisible());
        QVERIFY(QMetaObject::invokeMethod(aboutButton, "clicked"));
        QTRY_COMPARE(stack->property("depth").toInt(), 3);
        QTRY_VERIFY(!stack->property("busy").toBool());
        auto* page = window->findChild<QQuickItem*>("aboutPage");
        QVERIFY(page);
        QTRY_VERIFY(core.pulseScreen().watching());

        auto* version = page->findChild<QQuickItem*>("aboutVersion");
        QVERIFY(version);
        QVERIFY(!platform.version().isEmpty());
        QVERIFY(version->property("text").toString().contains(platform.version()));
        // The clouds fly.
        auto* backdrop = page->findChild<QQuickItem*>("cloudsBackdrop");
        QVERIFY(backdrop);
        const qreal before = backdrop->property("_time").toReal();
        QTRY_VERIFY(backdrop->property("_time").toReal() > before + 0.1);
        shot("about");

        auto* authors = page->findChild<QQuickItem*>("authors");
        auto* license = page->findChild<QQuickItem*>("license");
        QVERIFY(authors && license);
        QVERIFY(authors->isVisible() && !license->isVisible());
        // The tabs are a Repeater's: in the item tree, not the object tree.
        const auto tabNamed = [page](const char* name) {
            QQuickItem* found = nullptr;
            forEachItem(page, [&found, name](QQuickItem* item) {
                if (item->objectName() == QLatin1String(name))
                    found = item;
            });
            return found;
        };
        // Who helped, each with a way to reach them.
        auto* contributors = page->findChild<QQuickItem*>("contributors");
        QVERIFY(contributors && !contributors->isVisible());
        QVERIFY(tabNamed("contributorsTab"));
        QVERIFY(QMetaObject::invokeMethod(tabNamed("contributorsTab"), "clicked"));
        // The one picked slides in as the one left slides out: both are there
        // for a moment, the one left alone at the end.
        QTRY_VERIFY(contributors->isVisible());
        QVERIFY(authors->isVisible());
        QTRY_VERIFY(contributors->isVisible() && !authors->isVisible());
        QTRY_COMPARE(contributors->opacity(), 1.0);
        // A card each: the name, the email, the page.
        const auto cardTexts = [](QQuickItem* people) {
            QString all;
            int cards = 0;
            forEachItem(people, [&all, &cards](QQuickItem* item) {
                if (item->objectName() == QLatin1String("personName"))
                    ++cards;
                if (item->objectName().startsWith(QLatin1String("person")))
                    all += item->property("text").toString() + QLatin1Char('\n');
            });
            return std::make_pair(cards, all);
        };
        const auto [contributorCards, people] = cardTexts(contributors);
        QCOMPARE(contributorCards, 2);
        for (const char* part : { "Sergey Kostyuchenko", "mailto:derfenix@gmail.com", "github.com/derfenix",
                 "Vitaly Zdanevich", "https://github.com/vitaly-zdanevich" })
            QVERIFY2(people.contains(QLatin1String(part)), part);
        const auto [authorCards, authorsText] = cardTexts(authors);
        QCOMPARE(authorCards, 1);
        QVERIFY(authorsText.contains(QLatin1String("Vladislav Navrocky")));
        QVERIFY(authorsText.contains(QLatin1String("https://github.com/navrocky")));
        shot("about-contributors");

        // What it's made with, each by its logo, and the AI agents waving.
        auto* technologies = page->findChild<QQuickItem*>("technologies");
        QVERIFY(technologies && !technologies->isVisible());
        QVERIFY(QMetaObject::invokeMethod(tabNamed("technologiesTab"), "clicked"));
        QTRY_VERIFY(technologies->isVisible() && !contributors->isVisible());
        QList<QQuickItem*> mascots;
        int logos = 0;
        forEachItem(technologies, [&mascots, &logos](QQuickItem* item) {
            if (item->objectName() == QLatin1String("agentMascot") && item->isVisible())
                mascots.append(item);
            if (item->objectName() == QLatin1String("technologyLogo") && item->isVisible()) {
                ++logos;
                QTRY_COMPARE(item->property("status").toInt(), 1); // Image.Ready
            }
        });
        QCOMPARE(logos, 5);
        QCOMPARE(mascots.size(), 2);
        for (QQuickItem* mascot : mascots) {
            QVERIFY(mascot->property("running").toBool());
            QVERIFY(mascot->width() > 0 && mascot->height() > 0);
        }
        const int frame = mascots.first()->property("currentFrame").toInt();
        QTRY_VERIFY(mascots.first()->property("currentFrame").toInt() != frame);
        // The tabs on one line, scrolled to the one picked where they don't fit.
        auto* tabs = page->findChild<QQuickItem*>("aboutTabs");
        QVERIFY(tabs);
        const QQuickItem* picked = tabNamed("technologiesTab");
        QCOMPARE(picked->y(), tabNamed("authorsTab")->y());
        QTRY_VERIFY(picked->mapToItem(tabs, QPointF(0, 0)).x() >= 0
            && picked->mapToItem(tabs, QPointF(picked->width(), 0)).x() <= tabs->width());
        shot("about-technologies");

        QVERIFY(tabNamed("licenseTab"));
        QVERIFY(QMetaObject::invokeMethod(tabNamed("licenseTab"), "clicked"));
        QTRY_VERIFY(license->isVisible() && !authors->isVisible());
        // Reflowed: a paragraph is one line.
        QVERIFY(
            platform.licenseText().contains(QStringLiteral("Permission is hereby granted, free of charge, to any")));
        int paragraphs = 0;
        forEachItem(license, [&paragraphs](QQuickItem* item) {
            if (item->inherits("QQuickText"))
                ++paragraphs;
        });
        QVERIFY(paragraphs > 2); // a paragraph each
        // Within the screen's width.
        auto* content = page->findChild<QQuickItem*>("aboutContent");
        QVERIFY(content);
        QVERIFY(content->width() <= page->width());
        shot("about-license");

        QVERIFY(QMetaObject::invokeMethod(stack, "close"));
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        QTRY_VERIFY(!core.pulseScreen().watching());
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
        App::MobileCore core({ dir.filePath("core.ini") });
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
        QVERIFY(startScreenGone(window));

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
        // What's seldom needed is behind the list's menu: Settings there.
        {
            // The bar's, or the left column's with the screen turned to the
            // track (as a test before may have left it).
            auto* menuButton = window->findChild<QQuickItem*>("listMenu");
            if (menuButton == nullptr || !menuButton->isVisible())
                menuButton = window->findChild<QQuickItem*>("railMenu");
            QVERIFY(menuButton && menuButton->isVisible());
            QMetaObject::invokeMethod(menuButton, "clicked");
            // The bar's menu's, not the left column's.
            const auto shownEntry = [&window]() -> QQuickItem* {
                for (QQuickItem* entry : window->findChildren<QQuickItem*>("settingsEntry")) {
                    if (entry->isVisible())
                        return entry;
                }
                return nullptr;
            };
            QTRY_VERIFY(shownEntry() != nullptr);
            QQuickItem* settingsEntry = shownEntry();
            QTest::qWait(400);
            shot("main-menu");
            QMetaObject::invokeMethod(settingsEntry, "triggered");
            QTRY_COMPARE(window->findChild<QQuickItem*>("stack")->property("depth").toInt(), 2);
            QMetaObject::invokeMethod(window->findChild<QQuickItem*>("stack"), "close");
            QTRY_COMPARE(window->findChild<QQuickItem*>("stack")->property("depth").toInt(), 1);
        }
        // Scrolled, its rows don't come out over the bar above it.
        auto* mainList = window->findChild<QQuickItem*>("mainList");
        QVERIFY(mainList && (mainList->clip() || mainList->parentItem()->clip()));

        // The list's filter opens under its bar, and closes empty.
        playlist->setProperty("filterOpen", true);
        auto* filterField = window->findChild<QQuickItem*>("mainFilter");
        QVERIFY(filterField);
        filterField->setProperty("text", QStringLiteral("t2"));
        QTRY_COMPARE(backend.tracks()->rowCount(), 1);
        // Wide, it takes the title's place; on a phone, whose bar has the way to
        // the sources, it comes out under the bar.
        QTRY_VERIFY(filterField->isVisible());
        if (size.width() > size.height()) {
            auto* titleBlock = window->findChild<QQuickItem*>("listTitleBlock");
            QVERIFY(titleBlock && !titleBlock->isVisible());
        }
        QTest::qWait(300);
        const auto listTop = window->findChild<QQuickItem*>("mainList")->mapToScene(QPointF(0, 0)).y();
        QVERIFY(filterField->mapToScene(QPointF(0, filterField->height())).y() <= listTop);
        shot("main-filter");
        // Nothing matches: that's said, not the way to the Library.
        auto* nothingFound = window->findChild<QQuickItem*>("nothingFound");
        auto* welcome = window->findChild<QQuickItem*>("welcomePanel");
        QVERIFY(nothingFound && welcome && !nothingFound->isVisible());
        filterField->setProperty("text", QStringLiteral("no such track"));
        QTRY_COMPARE(backend.tracks()->rowCount(), 0);
        QTRY_COMPARE(nothingFound->opacity(), 1.0);
        QVERIFY(nothingFound->isVisible());
        QVERIFY(!welcome->isVisible());
        shot("main-nothing-found");
        playlist->setProperty("filterOpen", false);
        QCOMPARE(filterField->property("text").toString(), QString());
        QTRY_COMPARE(backend.tracks()->rowCount(), 2);
        QTRY_VERIFY(!nothingFound->isVisible());
        QVERIFY(!welcome->isVisible());

        // On a head unit, the two modes of the screen: everything has moved when
        // the animation is over, the source's button clear of the cover in both,
        // and turning back restores it all.
        if (size.width() > size.height()) {
            auto* bar = window->findChild<QQuickItem*>("sourceBar");
            auto* panel = window->findChild<QQuickItem*>("trackPanel");
            auto* cover = window->findChild<QQuickItem*>("trackCover");
            auto* details = window->findChild<QQuickItem*>("trackDetails");
            // What shows of it: the list itself keeps its width, and is cut off.
            auto* list = window->findChild<QQuickItem*>("mainList")->parentItem();
            auto* railMenu = window->findChild<QQuickItem*>("railMenu");
            auto* toggle = window->findChild<QObject*>("screenModeToggle");
            QVERIFY(bar && panel && cover && details && list && railMenu && toggle);
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
            const qreal normalTitleScale = window->findChild<QQuickItem*>("trackTitle")->scale();
            QVERIFY(!sceneRect(bar).intersects(normalCover));
            QVERIFY(!railMenu->isVisible());
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
            QVERIFY(window->findChild<QQuickItem*>("trackCoverPicture")->property("_showing").toBool());

            auto* playlistLine = findItem(window->contentItem(), QStringLiteral("trackPlaylist"));
            QVERIFY(playlistLine);
            const qreal lineWidth = playlistLine->width();
            QMetaObject::invokeMethod(toggle, "clicked");
            QTest::qWait(150);
            // Under way nothing with a shadow is laid out anew each frame.
            QCOMPARE(playlistLine->width(), lineWidth);
            if (const QString dir = qEnvironmentVariable("CLOUDMUS_MOBILE_SHOTS"); !dir.isEmpty()) {
                window->grabWindow().save(
                    QStringLiteral("%1/%2-mode-moving.png").arg(dir, QTest::currentDataTag()));
            }
            QTRY_VERIFY(settled("1"));
            // The list that names the playlist is folded away: it's named here, and
            // in the clear of what's on the way down and up from it.
            {
                auto* named = findItem(window->contentItem(), QStringLiteral("trackPlaylist"));
                auto* title = findItem(window->contentItem(), QStringLiteral("trackPlaylistTitle"));
                auto* details = findItem(window->contentItem(), QStringLiteral("trackDetails"));
                QVERIFY(named && title && details);
                QVERIFY(named->isVisible());
                QCOMPARE(named->opacity(), 1.0);
                QCOMPARE(title->property("text").toString(), core.activePlaylist().title());
                QVERIFY(!title->property("text").toString().isEmpty());
                const QRectF rect = named->mapRectToScene(named->boundingRect());
                QVERIFY(rect.top() >= 0);
                QVERIFY(rect.bottom() <= details->mapRectToScene(details->boundingRect()).top());
            }
            // A picture of the new size comes once it has settled, and is what's shown.
            QTRY_COMPARE(window->findChild<QQuickItem*>("trackCoverPicture")->property("_side").toInt(), qRound(window->findChild<QQuickItem*>("trackCoverPicture")->width() * window->devicePixelRatio()));
            // (Nothing to show if the fake's track, whose stream can't be fetched,
            // has been given up on by now: then it's the note.)
            if (!cover->property("_wanted").toString().isEmpty())
                QTRY_VERIFY(window->findChild<QQuickItem*>("trackCoverPicture")->property("_showing").toBool());
            else
                qInfo() << "cover: nothing wanted, hasTrack" << core.nowPlaying().hasTrack();
            if (!cover->property("_wanted").toString().isEmpty())
                QTRY_COMPARE(cover->property("shownSide").toInt(), window->findChild<QQuickItem*>("trackCoverPicture")->property("_side").toInt());
            shot("mode-expanded");
            const QRectF bigCover = sceneRect(cover);
            auto* titleBlock = window->findChild<QQuickItem*>("trackTitle");
            QVERIFY(titleBlock);
            QVERIFY(titleBlock->scale() > normalTitleScale);
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
            // And as far from the row's top as from its bottom.
            QQuickItem* row = rowCover;
            while (row && !QByteArray(row->metaObject()->className()).startsWith("TrackRow"))
                row = row->parentItem();
            QVERIFY(row);
            const QRectF rowRect = sceneRect(row);
            const QRectF coverRect = sceneRect(rowCover);
            QVERIFY2(qAbs((coverRect.top() - rowRect.top()) - (rowRect.bottom() - coverRect.bottom())) <= 1.0,
                qPrintable(QStringLiteral("cover %1..%2 in a row %3..%4")
                               .arg(coverRect.top()).arg(coverRect.bottom()).arg(rowRect.top()).arg(rowRect.bottom())));
            QVERIFY(sceneRect(details).left() >= bigCover.right());
            // Nothing of it pushed out of its place.
            QVERIFY(details->implicitWidth() <= details->width());
            QVERIFY(sceneRect(details).right() <= sceneRect(list).left());
            QVERIFY(railMenu->isVisible());
            QVERIFY(sceneRect(railMenu).bottom() > window->height() * 0.8);
            QVERIFY(sceneRect(railMenu).right() <= bigCover.left());

            QMetaObject::invokeMethod(toggle, "clicked");
            QTRY_VERIFY(settled("0"));
            QTRY_COMPARE(list->width(), normalList);
            QCOMPARE(bar->width(), normalBar);
            QVERIFY(!railMenu->isVisible());

            // The source's button goes to the Library, on the active playlist's source.
            QMetaObject::invokeMethod(bar, "clicked");
            QTRY_COMPARE(stack->property("depth").toInt(), 2);
            QCOMPARE(core.library().currentSourceId(), QStringLiteral("fake"));
            QMetaObject::invokeMethod(stack, "close");
            QTRY_COMPARE(stack->property("depth").toInt(), 1);
        }
        {
            // Each cover as far from its row's top as from its bottom.
            QTest::qWait(500);
            int checked = 0;
            const std::function<void(QQuickItem*)> each = [&](QQuickItem* item) {
                if (item->objectName() == QStringLiteral("rowCover")) {
                    QQuickItem* row = item;
                    while (row && !QByteArray(row->metaObject()->className()).startsWith("TrackRow"))
                        row = row->parentItem();
                    if (row && item->isVisible()) {
                        const QRectF r(row->mapToScene(QPointF(0, 0)), QSizeF(row->width(), row->height()));
                        const QRectF c(item->mapToScene(QPointF(0, 0)), QSizeF(item->width(), item->height()));
                        QVERIFY2(qAbs((c.top() - r.top()) - (r.bottom() - c.bottom())) <= 1.0, "cover off centre");
                        ++checked;
                    }
                }
                for (QQuickItem* child : item->childItems())
                    each(child);
            };
            each(playlist->findChild<QQuickItem*>("list") ? playlist->findChild<QQuickItem*>("list") : window->contentItem());
            QVERIFY(checked > 0);
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
            auto* nowPlaying = window->findChild<QQuickItem*>("nowPlaying");
            QVERIFY(nowPlaying);
            auto* miniTitle = findItem(nowPlaying, QStringLiteral("trackTitle"));
            auto* miniCover = findItem(nowPlaying, QStringLiteral("trackCoverPicture"));
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
        // A head unit has the track on its main screen: nothing comes over that.
        auto* nowPlayingSheet = window->findChild<QQuickItem*>("nowPlayingSheet");
        QVERIFY(nowPlayingSheet);
        QTRY_COMPARE(nowPlayingSheet->property("progress").toReal(), narrow ? 1.0 : 0.0);
        if (narrow) {
            shot("nowplaying");
            QMetaObject::invokeMethod(nowPlayingSheet, "close");
            QTRY_COMPARE(nowPlayingSheet->property("progress").toReal(), 0.0);
        }

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

        // Saving the playlist, behind a track that never ends (the fake's
        // "slow"): the header's button rings, the main screen's indicator
        // shows, and the Downloads page lists both.
        core.core().downloads().setEnabled(true);
        Track slow;
        slow.id = QStringLiteral("slow");
        slow.title = QStringLiteral("Slow one");
        core.core().downloads().downloadTrack(QStringLiteral("fake"), slow);
        // The header's, of the phone's or the wide layout's.
        const auto shownButton = [page]() -> QQuickItem* {
            for (QQuickItem* button : page->findChildren<QQuickItem*>("downloadPlaylist")) {
                if (button->isVisible())
                    return button;
            }
            return nullptr;
        };
        QTRY_VERIFY(shownButton() != nullptr);
        QQuickItem* downloadButton = shownButton();
        // A tap starts it; once it's under way, a tap goes on to the page,
        // where it can be stopped.
        QMetaObject::invokeMethod(downloadButton, "clicked");
        QTRY_COMPARE(downloadButton->property("downloading").toBool(), true);
        QCOMPARE(stack->property("depth").toInt(), 3);
        // The main screen's list menu rings meanwhile, and leads to the page.
        auto* listMenu = window->findChild<QQuickItem*>("listMenu");
        QVERIFY(listMenu);
        QTRY_COMPARE(listMenu->property("downloading").toBool(), true);
        auto* downloadsEntry = window->findChild<QObject*>("downloadsEntry");
        QVERIFY(downloadsEntry);
        QTRY_COMPARE(downloadsEntry->property("available").toBool(), true);
        shot("playlist-saving");
        QMetaObject::invokeMethod(downloadButton, "clicked");
        QTRY_COMPARE(stack->property("depth").toInt(), 4);
        QTRY_COMPARE(backend.downloads()->rowCount(), 2);
        // Once only, asked again.
        QMetaObject::invokeMethod(window, "openDownloads");
        QCOMPARE(stack->property("depth").toInt(), 4);
        shot("downloads");
        core.core().downloads().cancelAll();
        QTRY_COMPARE(downloadButton->property("downloading").toBool(), false);
        QTRY_COMPARE(listMenu->property("downloading").toBool(), false);
        // Over, still listed until cleared: the way to the page stays.
        QCOMPARE(playlist->property("hasDownloads").toBool(), true);
        core.core().downloads().clearFinished();
        QTRY_COMPARE(backend.downloads()->rowCount(), 0);
        QTRY_COMPARE(playlist->property("hasDownloads").toBool(), false);
        QCOMPARE(downloadsEntry->property("available").toBool(), false);
        core.core().downloads().setEnabled(false);
        QMetaObject::invokeMethod(stack, "close");
        QTRY_COMPARE(stack->property("depth").toInt(), 3);

        QMetaObject::invokeMethod(stack, "close");
        QTRY_COMPARE(stack->property("depth").toInt(), 2);
        // A message over the Library.
        core.core().messages().error(QStringLiteral("Something went wrong"));
        QObject* toasts = window->findChild<QObject*>("appToast");
        QTRY_COMPARE(toasts->property("count").toInt(), 1);
        // The cards are a list's delegates: reached by the item tree, not by QObject parents.
        std::function<void(QQuickItem*, QList<QObject*>&)> collectCards = [&](QQuickItem* item, QList<QObject*>& out) {
            if (item->objectName() == QLatin1String("toastCard"))
                out.append(item);
            for (QQuickItem* child : item->childItems())
                collectCards(child, out);
        };
        const auto cards = [&] {
            QList<QObject*> out;
            collectCards(qobject_cast<QQuickItem*>(toasts), out);
            return out;
        };
        QTRY_COMPARE(cards().size(), 1);
        QObject* first = cards().first();
        QCOMPARE(first->property("isError").toBool(), true);
        shot("message");
        // A message with a technical line behind it: stacks with the first one,
        // closable, and unfolding keeps it up.
        core.core().messages().error(QStringLiteral("YouTube Music: no network connection"),
            QStringLiteral("HTTPSConnectionPool(host='music.youtube.com'): Max retries exceeded"));
        QTRY_COMPARE(toasts->property("count").toInt(), 2);
        QObject* toast = nullptr;
        QTRY_VERIFY((toast = [&] {
            for (QObject* card : cards()) {
                if (!card->property("details").toString().isEmpty())
                    return card;
            }
            return static_cast<QObject*>(nullptr);
        }()) != nullptr);
        // The new one at the bottom, the first pushed up above it, both fully in.
        QQuickItem* older = qobject_cast<QQuickItem*>(first)->parentItem();
        QQuickItem* newer = qobject_cast<QQuickItem*>(toast)->parentItem();
        QTRY_COMPARE(newer->opacity(), 1.0);
        const qreal gap = toasts->property("spacing").toReal();
        QTRY_VERIFY(qAbs(older->y() + older->height() + gap - newer->y()) < 0.5);
        shot("messages");
        QObject* chevron = toast->findChild<QObject*>("toastChevron");
        QObject* close = toast->findChild<QObject*>("toastClose");
        QVERIFY(chevron != nullptr && close != nullptr);
        QCOMPARE(chevron->property("visible").toBool(), true);
        QMetaObject::invokeMethod(chevron, "clicked");
        QCOMPARE(toast->property("expanded").toBool(), true);
        QVERIFY(toast->property("shownText").toString().contains(QStringLiteral("Max retries")));
        // The first one runs out; the unfolded one stays.
        QTest::qWait(4500);
        QTRY_COMPARE(toasts->property("count").toInt(), 1);
        QMetaObject::invokeMethod(close, "clicked");
        QTRY_COMPARE(toasts->property("count").toInt(), 0);
    }
};

int main(int argc, char** argv)
{
    // The same binary doubles as the backend the tests start.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], Tests::kFakeBackendArgument) == 0)
            return Tests::runFakeBackend();
    }
    // The texts the tests look for are English, whatever the machine speaks.
    qputenv("LC_ALL", "en_US.UTF-8");
    qunsetenv("LANGUAGE");
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("cloudmus-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("cloudmus-mobile-tests"));
    // Anything a test saves goes to throwaway locations, not the user's.
    QStandardPaths::setTestModeEnabled(true);
    SharedCoreTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "SharedCoreTest.moc"
