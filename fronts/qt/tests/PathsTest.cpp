#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "Paths.h"

namespace Tests {

class PathsTest : public QObject {
    Q_OBJECT

private:
    static void write(const QString& path, const QByteArray& content = "x")
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    static QByteArray read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private slots:
    void theStateHomeIsXdgStateHomeWhenAbsolute()
    {
        QCOMPARE(Config::stateHomeFor(QStringLiteral("/var/state"), QStringLiteral("/home/me")),
            QStringLiteral("/var/state"));
    }

    void theStateHomeFallsBackToTheHomeDirectory()
    {
        // Unset, empty, and the relative value the XDG spec says to ignore.
        for (const QString& value : { QString(), QStringLiteral("state"), QStringLiteral("./state") }) {
            QCOMPARE(Config::stateHomeFor(value, QStringLiteral("/home/me")), QStringLiteral("/home/me/.local/state"));
        }
    }

    void theStateDirIsNeverTheRealOneInTests()
    {
        // Test mode sends it to a throwaway place, like every other location.
        QVERIFY(Config::stateDir().endsWith(QStringLiteral("/cloudmus/fronts/qt")));
#ifndef Q_OS_WIN
        QVERIFY2(Config::stateDir().contains(QStringLiteral(".qttest")), qPrintable(Config::stateDir()));
#endif
    }

    void migrationMovesLogCrashesAndAsanReports()
    {
        QTemporaryDir root;
        const QString from = root.filePath(QStringLiteral("config"));
        const QString to = root.filePath(QStringLiteral("state"));
        write(from + QStringLiteral("/debug.log"), "log");
        write(from + QStringLiteral("/crashes/crash-1.reported.txt"), "report");
        write(from + QStringLiteral("/crashes/sentry/a.run/b.envelope"), "event");
        write(from + QStringLiteral("/asan.log.123"), "asan");
        write(from + QStringLiteral("/config.ini"), "settings");

        Config::migrateLegacyState(from, to);

        QCOMPARE(read(to + QStringLiteral("/debug.log")), QByteArray("log"));
        QCOMPARE(read(to + QStringLiteral("/crashes/crash-1.reported.txt")), QByteArray("report"));
        QCOMPARE(read(to + QStringLiteral("/crashes/sentry/a.run/b.envelope")), QByteArray("event"));
        QCOMPARE(read(to + QStringLiteral("/asan.log.123")), QByteArray("asan"));
        // Not ours to move, and the old places are empty now.
        QCOMPARE(read(from + QStringLiteral("/config.ini")), QByteArray("settings"));
        QVERIFY(!QFile::exists(from + QStringLiteral("/debug.log")));
        QVERIFY(!QFile::exists(from + QStringLiteral("/crashes")));
        QVERIFY(!QFile::exists(from + QStringLiteral("/asan.log.123")));
    }

    void migrationKeepsWhatIsAlreadyThereAndMergesCrashDirectories()
    {
        QTemporaryDir root;
        const QString from = root.filePath(QStringLiteral("config"));
        const QString to = root.filePath(QStringLiteral("state"));
        write(from + QStringLiteral("/debug.log"), "old log");
        write(to + QStringLiteral("/debug.log"), "new log");
        write(from + QStringLiteral("/crashes/crash-1.reported.txt"), "old");
        write(from + QStringLiteral("/crashes/crash-2.txt"), "clash old");
        write(to + QStringLiteral("/crashes/crash-2.txt"), "clash new");
        write(to + QStringLiteral("/crashes/crash-3.txt"), "new");

        Config::migrateLegacyState(from, to);

        // A file in the way stays, with the old one beside it, undeleted.
        QCOMPARE(read(to + QStringLiteral("/debug.log")), QByteArray("new log"));
        QCOMPARE(read(from + QStringLiteral("/debug.log")), QByteArray("old log"));
        QCOMPARE(read(to + QStringLiteral("/crashes/crash-2.txt")), QByteArray("clash new"));
        QCOMPARE(read(from + QStringLiteral("/crashes/crash-2.txt")), QByteArray("clash old"));
        // A directory in the way is merged.
        QCOMPARE(read(to + QStringLiteral("/crashes/crash-1.reported.txt")), QByteArray("old"));
        QCOMPARE(read(to + QStringLiteral("/crashes/crash-3.txt")), QByteArray("new"));
    }

    void migrationDoesNothingWhenBothAreTheSameDirectory()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("debug.log")), "log");
        Config::migrateLegacyState(root.path(), root.path() + QStringLiteral("/"));
        QCOMPARE(read(root.filePath(QStringLiteral("debug.log"))), QByteArray("log"));
    }

    void migrationWithNothingToMoveCreatesNothingElse()
    {
        QTemporaryDir root;
        Config::migrateLegacyState(root.filePath(QStringLiteral("missing")), root.filePath(QStringLiteral("state")));
        QVERIFY(!QFile::exists(root.filePath(QStringLiteral("state"))));
        // An existing but empty config dir: the state dir is made, empty.
        QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("config"))));
        Config::migrateLegacyState(root.filePath(QStringLiteral("config")), root.filePath(QStringLiteral("state")));
        QVERIFY(QDir(root.filePath(QStringLiteral("state"))).isEmpty());
    }

    void migrationIsIdempotent()
    {
        QTemporaryDir root;
        const QString from = root.filePath(QStringLiteral("config"));
        const QString to = root.filePath(QStringLiteral("state"));
        write(from + QStringLiteral("/debug.log"), "log");
        Config::migrateLegacyState(from, to);
        Config::migrateLegacyState(from, to);
        QCOMPARE(read(to + QStringLiteral("/debug.log")), QByteArray("log"));
    }
};

QObject* makePathsTest() { return new PathsTest; }

} // namespace Tests

#include "PathsTest.moc"
