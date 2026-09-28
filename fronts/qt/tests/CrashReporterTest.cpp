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

namespace Tests {

namespace {

// The test binary started with this runs one crash scenario instead of the
// tests: `--crash-reporter-child <reportDir> <segv|throw|take>`.
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
    Diagnostics::CrashReporter::install({ QString::fromLocal8Bit(argv[2]), QStringLiteral("test-release") });
    Diagnostics::CrashReporter::addBreadcrumb(QStringLiteral("breadcrumb before the crash"));
    const QByteArray mode = argv[3];
    if (mode == "segv") {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    } else if (mode == "throw") {
        throw std::runtime_error("boom from the test");
    } else if (mode == "take") {
        for (const QString& report : Diagnostics::CrashReporter::takeNewReports())
            std::printf("%s\n", qPrintable(report));
    }
    return 0;
}

class CrashReporterTest : public QObject {
    Q_OBJECT

private:
    static QProcess* runChild(const QString& dir, const char* mode)
    {
        auto* child = new QProcess;
        child->start(QCoreApplication::applicationFilePath(),
            { QString::fromLatin1(kChildArgument), dir, QString::fromLatin1(mode) });
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
};

QObject* makeCrashReporterTest() { return new CrashReporterTest; }

} // namespace Tests

#include "CrashReporterTest.moc"
