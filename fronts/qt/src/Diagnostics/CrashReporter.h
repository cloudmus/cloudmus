#pragma once

#include <QString>
#include <QStringList>

// Writes a report when the app crashes: a fatal signal on Linux, an
// unhandled SEH exception on Windows, or std::terminate/abort on either.
// A report is a text file (release, OS, reason, stack as module+offset,
// the last log lines) in the report directory; on Windows a minidump sits
// next to it. Everything the crash handler needs is prepared up front,
// since the handler itself may not allocate or touch Qt.
//
// With a Sentry DSN (and a build that has sentry-native), crashes are also
// sent there: the inproc backend stores the event under reportDir/sentry
// and uploads it on the next start. The local report stays either way —
// it is what a run without a DSN, or with reporting switched off, has.
namespace Diagnostics::CrashReporter {

struct Options {
    QString reportDir;
    QString release;
    // Empty: nothing leaves the machine (also when the build lacks Sentry).
    QString sentryDsn;
};

// As early as possible in main(), once: installs the platform handlers.
void install(const Options& options);

// Crashes the process with a real segmentation fault, the way a bug would —
// for checking the whole path (see CrashShortcut). Never returns.
[[noreturn]] void crashOnPurpose();

// Flushes what Sentry has pending; before the process exits normally.
void shutdown();

// One line of recent activity. The last few go into the report; Logging
// feeds every log message here, whether debug logging is on or not.
// Cheap, thread-safe, never blocks.
void addBreadcrumb(const QString& message);

// Reports written by earlier runs that nobody has picked up yet. Taking
// them marks them picked up; only the newest few are kept on disk.
QStringList takeNewReports();

} // namespace Diagnostics::CrashReporter
