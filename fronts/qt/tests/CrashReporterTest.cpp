#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <cstring>
#include <stdexcept>

#ifndef Q_OS_WIN
#include <sys/resource.h>
#endif

#include "CrashReporter.h"
#include "CrashShortcut.h"
#include "Settings.h"

namespace Tests {

namespace {

// The test binary started with this runs one crash scenario instead of the
// tests: `--crash-reporter-child <reportDir> <segv|throw|take|sentry-dir> [dsn]`.
constexpr char kChildArgument[] = "--crash-reporter-child";

} // namespace

int runCrashReporterChildIfAsked(int argc, char** argv)
{
    if (argc < 4 || std::strcmp(argv[1], kChildArgument) != 0)
        return -1;
#ifndef Q_OS_WIN
    // A crash on purpose: no core dump for the system to collect.
    const rlimit noCore { 0, 0 };
    setrlimit(RLIMIT_CORE, &noCore);
#endif
    Diagnostics::CrashReporter::install({ QString::fromLocal8Bit(argv[2]), QStringLiteral("test-release"),
        argc > 4 ? QString::fromLocal8Bit(argv[4]) : QString() });
    Diagnostics::CrashReporter::addBreadcrumb(QStringLiteral("breadcrumb before the crash"));
    const QByteArray mode = argv[3];
    if (mode == "segv") {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    } else if (mode == "throw") {
        throw std::runtime_error("boom from the test");
    } else if (mode == "on-purpose") {
        Diagnostics::CrashReporter::crashOnPurpose();
    } else if (mode == "sentry-dir") {
        std::printf("%d\n", QDir(QString::fromLocal8Bit(argv[2]) + QStringLiteral("/sentry")).exists() ? 1 : 0);
    } else if (mode == "take") {
        for (const QString& report : Diagnostics::CrashReporter::takeNewReports())
            std::printf("%s\n", qPrintable(report));
    }
    return 0;
}

class CrashReporterTest : public QObject {
    Q_OBJECT

private:
    static QProcess* runChild(const QString& dir, const char* mode, const QString& dsn = QString())
    {
        auto* child = new QProcess;
        QStringList arguments { QString::fromLatin1(kChildArgument), dir, QString::fromLatin1(mode) };
        if (!dsn.isEmpty())
            arguments.append(dsn);
        child->start(QCoreApplication::applicationFilePath(), arguments);
        child->waitForFinished(10000);
        return child;
    }

    static QStringList reports(const QString& dir)
    {
        return QDir(dir).entryList({ QStringLiteral("crash-*.txt") }, QDir::Files);
    }

    static QByteArray readReport(const QString& dir)
    {
        const QStringList names = reports(dir);
        if (names.size() != 1)
            return { };
        QFile file(QDir(dir).filePath(names.first()));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private slots:
    void aSegfaultLeavesAReportAndStillCrashes()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX signal handling");
#endif
        QTemporaryDir dir;
        std::unique_ptr<QProcess> child(runChild(dir.path(), "segv"));
        QCOMPARE(child->exitStatus(), QProcess::CrashExit);

        const QByteArray report = readReport(dir.path());
        QVERIFY2(!report.isEmpty(), "exactly one report expected");
        QVERIFY(report.contains("release: test-release"));
        QVERIFY(report.contains("reason: SIGSEGV (11) at 0x0"));
        // The crashing function is ours, exported for the stack to name it.
        QVERIFY(report.contains("runCrashReporterChildIfAsked"));
        QVERIFY(report.contains("breadcrumb before the crash"));
    }

    void anUncaughtExceptionLeavesOneReportWithItsMessage()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX signal handling");
#endif
        QTemporaryDir dir;
        std::unique_ptr<QProcess> child(runChild(dir.path(), "throw"));
        QCOMPARE(child->exitStatus(), QProcess::CrashExit);

        // One report, not a second one from the abort() that follows.
        const QByteArray report = readReport(dir.path());
        QVERIFY2(!report.isEmpty(), "exactly one report expected");
        QVERIFY(report.contains("reason: std::terminate: uncaught exception: boom from the test"));
        QVERIFY(report.contains("breadcrumb before the crash"));
    }

    void aReportIsTakenOnlyOnce()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX signal handling");
#endif
        QTemporaryDir dir;
        std::unique_ptr<QProcess>(runChild(dir.path(), "segv"));

        std::unique_ptr<QProcess> first(runChild(dir.path(), "take"));
        const QList<QByteArray> taken = first->readAllStandardOutput().trimmed().split('\n');
        QCOMPARE(taken.size(), 1);
        QVERIFY(QFile::exists(QString::fromLocal8Bit(taken.first())));

        std::unique_ptr<QProcess> second(runChild(dir.path(), "take"));
        QCOMPARE(second->readAllStandardOutput().trimmed(), QByteArray());
    }

    void crashingOnPurposeLeavesAReport()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX signal handling");
#endif
        QTemporaryDir dir;
        std::unique_ptr<QProcess> child(runChild(dir.path(), "on-purpose"));
        QCOMPARE(child->exitStatus(), QProcess::CrashExit);
        const QByteArray report = readReport(dir.path());
        QVERIFY2(!report.isEmpty(), "exactly one report expected");
        QVERIFY(report.contains("reason: SIGSEGV"));
    }

    void theCrashShortcutNeedsThreePressesInTime()
    {
        int crashes = 0;
        Diagnostics::CrashShortcut shortcut([&crashes] { ++crashes; });
        QObject target;
        const auto press = [&](int key, Qt::KeyboardModifiers modifiers, quint64 at, bool repeat = false) {
            QKeyEvent event(QEvent::KeyPress, key, modifiers, QString(), repeat);
            event.setTimestamp(at);
            QVERIFY(!shortcut.eventFilter(&target, &event));
        };
        const Qt::KeyboardModifiers chord = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier;

        press(Qt::Key_F12, chord, 1000);
        press(Qt::Key_F12, chord, 1500);
        QCOMPARE(crashes, 0);
        press(Qt::Key_F12, chord, 2000);
        QCOMPARE(crashes, 1);

        // Too slow: the run starts over.
        press(Qt::Key_F12, chord, 10000);
        press(Qt::Key_F12, chord, 11000);
        press(Qt::Key_F12, chord, 14000);
        QCOMPARE(crashes, 1);

        // Other keys, missing modifiers and a held key don't count.
        press(Qt::Key_F11, chord, 20000);
        press(Qt::Key_F12, Qt::ControlModifier | Qt::ShiftModifier, 20100);
        press(Qt::Key_F12, chord, 20200, true);
        press(Qt::Key_F12, chord, 20300);
        press(Qt::Key_F12, chord, 20400);
        QCOMPARE(crashes, 1);
    }

    void crashReportsAreOnUntilSwitchedOff()
    {
        Config::Settings settings;
        settings.setCrashReportsEnabled(true);
        QVERIFY(settings.crashReportsEnabled());
        QVERIFY(Config::Settings::crashReportsEnabledOnDisk());
        settings.setCrashReportsEnabled(false);
        QVERIFY(!settings.crashReportsEnabled());
        // What main() reads before Settings exists.
        QVERIFY(!Config::Settings::crashReportsEnabledOnDisk());
        settings.setCrashReportsEnabled(true);
    }

    void aCrashWithSentryOnStillLeavesTheLocalReport()
    {
#if !defined(CLOUDMUS_HAS_SENTRY)
        QSKIP("built without sentry-native");
#elif defined(Q_OS_WIN)
        QSKIP("POSIX signal handling");
#endif
        QTemporaryDir dir;
        std::unique_ptr<QProcess> child(
            runChild(dir.path(), "segv", QStringLiteral("https://abc123@o1.ingest.sentry.io/42")));
        QCOMPARE(child->exitStatus(), QProcess::CrashExit);
        QVERIFY2(!readReport(dir.path()).isEmpty(), "the local report is still written");
        // And Sentry stored its event, to send at the next start.
        QVERIFY(QDir(dir.filePath(QStringLiteral("sentry"))).exists());
    }

    void withoutADsnNothingIsKeptForSentry()
    {
#if !defined(CLOUDMUS_HAS_SENTRY)
        QSKIP("built without sentry-native");
#endif
        QTemporaryDir dir;
        // Events left from a run that had reporting on.
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("sentry/old.run"))));
        std::unique_ptr<QProcess> child(runChild(dir.path(), "sentry-dir"));
        QCOMPARE(child->readAllStandardOutput().trimmed(), QByteArray("0"));
    }
};

QObject* makeCrashReporterTest() { return new CrashReporterTest; }

} // namespace Tests

#include "CrashReporterTest.moc"
