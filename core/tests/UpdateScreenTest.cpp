#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "FakeUpstream.h"
#include "ReleaseFeed.h"
#include "Settings.h"
#include "UpdateChecker.h"
#include "UpdateScreen.h"

namespace Tests {

namespace {

using ViewModel::UpdateScreen;

// A release with this platform's file, served by `upstream` at /<tag>.
QJsonObject release(const FakeUpstream& upstream, const QString& tag, const QString& body)
{
    const QString name = QStringLiteral("CloudMus-%1%2").arg(tag, Update::platformAssetSuffix());
    return QJsonObject {
        { QStringLiteral("tag_name"), tag },
        { QStringLiteral("body"), body },
        { QStringLiteral("html_url"), QStringLiteral("https://example.org/releases/") + tag },
        { QStringLiteral("published_at"), QStringLiteral("2026-09-10T12:00:00Z") },
        { QStringLiteral("draft"), false },
        { QStringLiteral("prerelease"), false },
        { QStringLiteral("assets"),
            QJsonArray { QJsonObject {
                { QStringLiteral("name"), name },
                { QStringLiteral("browser_download_url"), upstream.url(QStringLiteral("/") + tag) },
                { QStringLiteral("size"), 4096 },
            } } },
    };
}

// The screen over a checker of version `current`, the feed at /feed.
struct Fixture {
    explicit Fixture(const QString& current)
    {
        settings.setSkippedUpdateVersion(QString());
        checker.configure(current, upstream.url(QStringLiteral("/feed")));
    }
    ~Fixture() { settings.setSkippedUpdateVersion(QString()); }

    void serve(const QJsonArray& releases)
    {
        upstream.bodies.insert(QStringLiteral("/feed"), QJsonDocument(releases).toJson());
    }

    FakeUpstream upstream;
    Config::Settings settings;
    Update::UpdateChecker checker { settings };
    UpdateScreen screen { checker };
};

} // namespace

class UpdateScreenTest : public QObject {
    Q_OBJECT

private slots:
    void versionCodesMatchTheApk()
    {
        // fronts/mobile/CMakeLists.txt: 0.7.0 → 700000, 1.12.3-5 → 11203005.
        const auto version = Update::versionFromCode(11203005);
        QVERIFY(version);
        QCOMPARE(version->major, 1);
        QCOMPARE(version->minor, 12);
        QCOMPARE(version->patch, 3);
        QVERIFY(version->dev);
        QCOMPARE(Update::versionFromCode(700000)->toString(), QStringLiteral("0.7.0"));
        QVERIFY(!Update::versionFromCode(0));
    }

    void offersAndDownloads()
    {
        Fixture f(QStringLiteral("0.6.0"));
        f.serve({ release(f.upstream, QStringLiteral("0.7.0"), QStringLiteral("- seven")),
            release(f.upstream, QStringLiteral("0.6.1"), QString()) });
        f.upstream.bodies.insert(QStringLiteral("/0.7.0"), FakeUpstream::pattern(4096));
        QTemporaryDir dir;
        f.screen.setDownloadPath([&dir](const Update::Release& r) { return dir.filePath(r.assetName); });
        QSignalSpy offered(&f.screen, &UpdateScreen::offered);

        f.screen.checkAutomatically();
        QTRY_COMPARE(offered.size(), 1);
        QCOMPARE(offered.first().first().toBool(), false);
        QCOMPARE(f.screen.step(), int(UpdateScreen::Offer));
        QCOMPARE(f.screen.via(), int(UpdateScreen::Here));
        QCOMPARE(f.screen.version(), QStringLiteral("0.7.0"));
        QVERIFY(f.screen.notes().contains(QStringLiteral("- seven")));
        // Every release since the running one, the older with no notes.
        QVERIFY(f.screen.notes().contains(QStringLiteral("**0.6.1**")));

        f.screen.install();
        QCOMPARE(f.screen.step(), int(UpdateScreen::Downloading));
        QTRY_COMPARE(f.screen.step(), int(UpdateScreen::Ready));
        const QString file = f.screen.downloadedFile();
        QVERIFY(QFile::exists(file));

        // The system's question turned down: the file stays, to try again.
        f.screen.installStarted();
        QCOMPARE(f.screen.step(), int(UpdateScreen::Installing));
        f.screen.installCancelled();
        QCOMPARE(f.screen.step(), int(UpdateScreen::Ready));
        QCOMPARE(f.screen.downloadedFile(), file);

        // Backing out removes it.
        f.screen.close();
        QCOMPARE(f.screen.step(), int(UpdateScreen::None));
        QVERIFY(!QFile::exists(file));
        QVERIFY(f.screen.available());
    }

    void skippedStaysQuietUntilAskedFor()
    {
        Fixture f(QStringLiteral("0.6.0"));
        f.serve({ release(f.upstream, QStringLiteral("0.7.0"), QString()) });
        QSignalSpy offered(&f.screen, &UpdateScreen::offered);
        f.screen.checkAutomatically();
        QTRY_COMPARE(offered.size(), 1);
        f.screen.skip();
        QCOMPARE(f.screen.step(), int(UpdateScreen::None));

        f.screen.checkAutomatically();
        QTest::qWait(300);
        QCOMPARE(offered.size(), 1);

        f.screen.check();
        QTRY_COMPARE(offered.size(), 2);
        QCOMPARE(offered.last().first().toBool(), true);
        QVERIFY(!f.screen.checking());
    }

    void upToDateIsToldWhenAskedFor()
    {
        Fixture f(QStringLiteral("0.7.0"));
        f.serve({ release(f.upstream, QStringLiteral("0.7.0"), QString()) });
        f.screen.check();
        QVERIFY(f.screen.checking());
        QTRY_VERIFY(!f.screen.checking());
        QVERIFY(!f.screen.checkHeading().isEmpty());
        QVERIFY(f.screen.checkText().contains(QStringLiteral("0.7.0")));
        QVERIFY(!f.screen.checkFailed());
        QCOMPARE(f.screen.step(), int(UpdateScreen::None));
    }

    void storeVersionLimitsTheNotes()
    {
        // GitHub has 0.8.0 already; the store only 0.7.0.
        Fixture f(QStringLiteral("0.6.0"));
        f.serve({ release(f.upstream, QStringLiteral("0.8.0"), QStringLiteral("- eight")),
            release(f.upstream, QStringLiteral("0.7.0"), QStringLiteral("- seven")) });
        QSignalSpy offered(&f.screen, &UpdateScreen::offered);
        f.screen.offerStoreVersion(700000, false);
        QTRY_COMPARE(offered.size(), 1);
        QCOMPARE(f.screen.via(), int(UpdateScreen::Store));
        QCOMPARE(f.screen.version(), QStringLiteral("0.7.0"));
        QVERIFY(f.screen.notes().contains(QStringLiteral("- seven")));
        QVERIFY(!f.screen.notes().contains(QStringLiteral("- eight")));

        // The store's flow runs; left, it's offered again.
        f.screen.install();
        QCOMPARE(f.screen.step(), int(UpdateScreen::Installing));
        f.screen.installCancelled();
        QCOMPARE(f.screen.step(), int(UpdateScreen::Offer));

        // No feed at all: the store's version alone.
        f.upstream.forbidden.insert(QStringLiteral("/feed"));
        f.screen.close();
        f.screen.offerStoreVersion(900000, true);
        QTRY_COMPARE(offered.size(), 2);
        QCOMPARE(f.screen.version(), QStringLiteral("0.9.0"));

        // Not newer than this one: nothing.
        f.screen.offerStoreVersion(600000, false);
        QTest::qWait(200);
        QCOMPARE(offered.size(), 2);
    }

    void releasePageWhenItCantInstall()
    {
        Fixture f(QStringLiteral("0.6.0"));
        f.serve({ release(f.upstream, QStringLiteral("0.7.0"), QString()) });
        f.screen.setInstallUnavailable(QStringLiteral("Not from here"));
        QSignalSpy offered(&f.screen, &UpdateScreen::offered);
        QSignalSpy steps(&f.screen, &UpdateScreen::stepTaken);
        f.screen.checkAutomatically();
        QTRY_COMPARE(offered.size(), 1);
        QCOMPARE(f.screen.via(), int(UpdateScreen::ReleasePage));
        QCOMPARE(f.screen.unavailableReason(), QStringLiteral("Not from here"));
        f.screen.install();
        QCOMPARE(f.screen.step(), int(UpdateScreen::Offer));
        QCOMPARE(steps.size(), 1);
        QCOMPARE(steps.first().first().toString(), QStringLiteral("release_page"));
    }
};

QObject* makeUpdateScreenTest() { return new UpdateScreenTest; }

} // namespace Tests

#include "UpdateScreenTest.moc"
