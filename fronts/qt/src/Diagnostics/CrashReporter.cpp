#include "CrashReporter.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <QDir>
#include <QRegularExpression>
#include <QSysInfo>

#ifdef CLOUDMUS_HAS_SENTRY
#include <sentry.h>
#endif

#include "CrashReporterDetail.h"

namespace Diagnostics::CrashReporter {

namespace {

// How many reports taken by takeNewReports() stay on disk.
constexpr int kKeptReports = 10;

constexpr std::size_t kBreadcrumbCount = 40;
constexpr std::size_t kBreadcrumbBytes = 512;

// Filled once by install(), read by the crash handlers.
char g_reportDir[1024];
char g_release[128];
char g_os[256];

// A ring of the last log lines. A crash while one is being written may
// catch it half-copied — acceptable for a report, and no lock is needed.
char g_breadcrumbs[kBreadcrumbCount][kBreadcrumbBytes];
std::atomic<std::uint64_t> g_breadcrumbNext { 0 };

void copyTruncated(char* destination, std::size_t size, const QByteArray& source)
{
    const std::size_t length = std::min<std::size_t>(size - 1, std::size_t(source.size()));
    std::memcpy(destination, source.constData(), length);
    destination[length] = '\0';
}

#ifdef CLOUDMUS_HAS_SENTRY
std::atomic<bool> g_sentryActive { false };

// A tagged release (x.y.z) is "production"; anything git describe adds
// (-5-gabc, -dirty) is somebody's own build.
const char* sentryEnvironment(const QString& release)
{
    static const QRegularExpression tagged(QStringLiteral(R"(^\d+\.\d+\.\d+$)"));
    return tagged.match(release).hasMatch() ? "production" : "development";
}

// Log lines go up as breadcrumbs; a URL's query string (signed stream
// links, tokens) must not.
QByteArray scrubbed(const QString& message)
{
    static const QRegularExpression query(QStringLiteral(R"((https?://[^\s?#"']+)\?[^\s"']*)"));
    QString result = message;
    result.replace(query, QStringLiteral("\\1?…"));
    return result.toUtf8();
}

void startSentry(const Options& options)
{
    const QString database = QDir::toNativeSeparators(options.reportDir + QStringLiteral("/sentry"));
    sentry_options_t* sentry = sentry_options_new();
    sentry_options_set_dsn(sentry, options.sentryDsn.toUtf8().constData());
    sentry_options_set_release(sentry, (QStringLiteral("cloudmus-qt@") + options.release).toUtf8().constData());
    sentry_options_set_environment(sentry, sentryEnvironment(options.release));
    sentry_options_set_database_path(sentry, database.toUtf8().constData());
    sentry_options_set_max_breadcrumbs(sentry, int(kBreadcrumbCount));
    // Crashes only: no session health, nothing about a user.
    sentry_options_set_auto_session_tracking(sentry, 0);
    sentry_options_set_require_user_consent(sentry, 0);
    if (sentry_init(sentry) == 0)
        g_sentryActive.store(true, std::memory_order_release);
}
#endif

void onTerminate()
{
    // Allocating is fine here, unlike in a signal handler: terminate runs
    // on an intact thread.
    const char* what = nullptr;
    if (std::exception_ptr exception = std::current_exception()) {
        try {
            std::rethrow_exception(exception);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
            what = "(not a std::exception)";
        }
    }
    if (Detail::beginReport(Detail::unixTimeNow())) {
        Detail::writeHeader();
        Detail::write("reason: std::terminate");
        if (what != nullptr) {
            Detail::write(": uncaught exception: ");
            Detail::write(what);
        }
        Detail::write("\n\nstack:\n");
        Detail::writeCurrentStack();
        Detail::writeBreadcrumbs();
        Detail::endReport();
    }
    Detail::disarmAbortHandler();
    std::abort();
}

} // namespace

void install(const Options& options)
{
    QDir().mkpath(options.reportDir);
    copyTruncated(g_reportDir, sizeof g_reportDir, QDir::toNativeSeparators(options.reportDir).toUtf8());
    copyTruncated(g_release, sizeof g_release, options.release.toUtf8());
    copyTruncated(g_os, sizeof g_os,
        (QSysInfo::prettyProductName() + QStringLiteral(" (") + QSysInfo::kernelType() + QLatin1Char(' ')
            + QSysInfo::kernelVersion() + QStringLiteral(", ") + QSysInfo::currentCpuArchitecture() + QLatin1Char(')'))
            .toUtf8());
    Detail::installPlatformHandlers();
    std::set_terminate(onTerminate);
#ifdef CLOUDMUS_HAS_SENTRY
    // After our own handlers: Sentry's inproc handler remembers the ones
    // installed before it and calls them once its event is stored, so the
    // local report is still written.
    if (!options.sentryDsn.isEmpty())
        startSentry(options);
    else
        // Switched off: events stored earlier must not be sent by a later
        // run that has it on again.
        QDir(options.reportDir + QStringLiteral("/sentry")).removeRecursively();
#endif
}

void crashOnPurpose()
{
    // A write through a null pointer, not abort(): that is the signal a real
    // bug raises, and what the reporter's handlers are for. volatile keeps
    // the compiler from reasoning the undefined behaviour away.
    volatile int* nowhere = nullptr;
    *nowhere = 1;
    std::abort();
}

void shutdown()
{
#ifdef CLOUDMUS_HAS_SENTRY
    if (g_sentryActive.exchange(false))
        sentry_close();
#endif
}

void addBreadcrumb(const QString& message)
{
    const std::uint64_t index = g_breadcrumbNext.fetch_add(1, std::memory_order_relaxed);
    copyTruncated(g_breadcrumbs[index % kBreadcrumbCount], kBreadcrumbBytes, message.toUtf8());
#ifdef CLOUDMUS_HAS_SENTRY
    if (g_sentryActive.load(std::memory_order_acquire))
        sentry_add_breadcrumb(sentry_value_new_breadcrumb("default", scrubbed(message).constData()));
#endif
}

QStringList takeNewReports()
{
    QDir dir(QString::fromUtf8(g_reportDir));
    static const QRegularExpression fresh(QStringLiteral(R"(^crash-(\d+)\.txt$)"));
    QStringList taken;
    for (const QString& name : dir.entryList({ QStringLiteral("crash-*.txt") }, QDir::Files, QDir::Name)) {
        const QRegularExpressionMatch match = fresh.match(name);
        if (!match.hasMatch())
            continue;
        const QString reported = QStringLiteral("crash-%1.reported.txt").arg(match.captured(1));
        if (dir.rename(name, reported))
            taken.append(dir.filePath(reported));
    }

    // Oldest first (the names carry the time), all but the newest few go.
    QStringList old = dir.entryList({ QStringLiteral("crash-*.reported.txt") }, QDir::Files, QDir::Name);
    while (old.size() > kKeptReports) {
        const QString name = old.takeFirst();
        dir.remove(name);
        dir.remove(QString(name).replace(QStringLiteral(".reported.txt"), QStringLiteral(".dmp")));
    }
    return taken;
}

namespace Detail {

void write(const char* text) { write(text, std::strlen(text)); }

void writeDecimal(std::uint64_t value)
{
    char digits[21];
    std::size_t position = sizeof digits;
    do {
        digits[--position] = char('0' + value % 10);
        value /= 10;
    } while (value != 0);
    write(digits + position, sizeof digits - position);
}

void writeHex(std::uint64_t value)
{
    char digits[18];
    std::size_t position = sizeof digits;
    do {
        digits[--position] = "0123456789abcdef"[value & 0xf];
        value >>= 4;
    } while (value != 0);
    digits[--position] = 'x';
    digits[--position] = '0';
    write(digits + position, sizeof digits - position);
}

void writeHeader()
{
    write("CloudMus crash report\nrelease: ");
    write(g_release);
    write("\nos: ");
    write(g_os);
    write("\ntime: ");
    writeDecimal(unixTimeNow());
    write(" (unix)\n");
}

void writeBreadcrumbs()
{
    write("\nrecent log (oldest first):\n");
    const std::uint64_t next = g_breadcrumbNext.load(std::memory_order_relaxed);
    const std::uint64_t first = next > kBreadcrumbCount ? next - kBreadcrumbCount : 0;
    for (std::uint64_t i = first; i < next; ++i) {
        const char* line = g_breadcrumbs[i % kBreadcrumbCount];
        write(line, strnlen(line, kBreadcrumbBytes));
        write("\n");
    }
}

const char* reportDirUtf8() { return g_reportDir; }

} // namespace Detail

} // namespace Diagnostics::CrashReporter
