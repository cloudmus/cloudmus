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
// Local only for now. The API mirrors what a crash service's SDK
// (sentry-native: init, breadcrumbs, pending reports) needs, so it can be
// swapped in behind it later without touching the callers.
namespace Diagnostics::CrashReporter {

struct Options {
    QString reportDir;
    QString release;
};

// As early as possible in main(), once: installs the platform handlers.
void install(const Options& options);

// One line of recent activity. The last few go into the report; Logging
// feeds every log message here, whether debug logging is on or not.
// Cheap, thread-safe, never blocks.
void addBreadcrumb(const QString& message);

// Reports written by earlier runs that nobody has picked up yet. Taking
// them marks them picked up; only the newest few are kept on disk.
QStringList takeNewReports();

} // namespace Diagnostics::CrashReporter
