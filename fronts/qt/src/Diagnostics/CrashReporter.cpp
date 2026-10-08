#include "CrashReporter.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUrl>

#ifdef CLOUDMUS_HAS_SENTRY
#include <sentry.h>
#endif

#include "CrashReporterDetail.h"

namespace Diagnostics::CrashReporter {

namespace {

Q_LOGGING_CATEGORY(lcSentry, "cloudmus.diagnostics.sentry")

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

// The SDK's own log lines, forwarded to ours (shown with --debug, warnings
// always). Inside the crash handler nothing may allocate, which Qt's logging
// does: the SDK's first line there is "entering signal handler", and from it
// on lines go straight to stderr.
std::atomic<bool> g_inCrashHandler { false };
// Set while an SDK line is being logged: it must not come back through
// Logging as a breadcrumb, into the SDK that may be holding its own lock.
thread_local bool t_inSdkLog = false;

void sdkLog(sentry_level_t level, const char* format, va_list args, void*)
{
    char text[512];
    std::vsnprintf(text, sizeof text, format, args);
    if (g_inCrashHandler.load(std::memory_order_relaxed) || std::strncmp(text, "entering signal handler", 23) == 0) {
        g_inCrashHandler.store(true, std::memory_order_relaxed);
        std::fputs("sentry: ", stderr);
        std::fputs(text, stderr);
        std::fputc('\n', stderr);
        return;
    }
    t_inSdkLog = true;
    switch (level) {
        case SENTRY_LEVEL_DEBUG:
            qCDebug(lcSentry).noquote() << "sdk:" << text;
            break;
        case SENTRY_LEVEL_INFO:
            qCInfo(lcSentry).noquote() << "sdk:" << text;
            break;
        case SENTRY_LEVEL_WARNING:
            qCWarning(lcSentry).noquote() << "sdk:" << text;
            break;
        default:
            qCCritical(lcSentry).noquote() << "sdk:" << text;
            break;
    }
    t_inSdkLog = false;
}

#ifndef Q_OS_WIN
// libcurl looks for the CA bundle where the distribution it was built on
// keeps it (Debian: /etc/ssl/certs/ca-certificates.crt), and fails every
// request when the file isn't there — as on openSUSE or Fedora. The
// AppImage's libcurl is Debian's, so look for the bundle ourselves.
QString findCaBundle()
{
    QStringList candidates;
    if (const QByteArray fromEnvironment = qgetenv("SSL_CERT_FILE"); !fromEnvironment.isEmpty())
        candidates.append(QString::fromLocal8Bit(fromEnvironment));
    candidates << QStringLiteral("/etc/ssl/certs/ca-certificates.crt") // Debian, Ubuntu, Arch
               << QStringLiteral("/etc/pki/tls/certs/ca-bundle.crt") // Fedora, RHEL
               << QStringLiteral("/etc/ssl/ca-bundle.pem") // openSUSE
               << QStringLiteral("/var/lib/ca-certificates/ca-bundle.pem") // openSUSE
               << QStringLiteral("/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem")
               << QStringLiteral("/etc/ssl/cert.pem"); // Alpine, others
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isReadable())
            return candidate;
    }
    return { };
}
#endif

int countUnsentEvents(const QString& database)
{
    int count = 0;
    QDirIterator it(database, { QStringLiteral("*.envelope") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

void startSentry(const Options& options)
{
    const QString database = QDir::toNativeSeparators(options.reportDir + QStringLiteral("/sentry"));
    const char* environment = sentryEnvironment(options.release);
    sentry_options_t* sentry = sentry_options_new();
    sentry_options_set_dsn(sentry, options.sentryDsn.toUtf8().constData());
    sentry_options_set_release(sentry, (QStringLiteral("cloudmus-qt@") + options.release).toUtf8().constData());
    sentry_options_set_environment(sentry, environment);
    sentry_options_set_database_path(sentry, database.toUtf8().constData());
    sentry_options_set_max_breadcrumbs(sentry, int(kBreadcrumbCount));
    // Crashes only: no session health, nothing about a user.
    sentry_options_set_auto_session_tracking(sentry, 0);
    sentry_options_set_require_user_consent(sentry, 0);
    // The SDK logs, and its libcurl prints every request to stderr, only
    // when asked: it is also the only place a failed send shows up.
    sentry_options_set_debug(sentry, options.sentryDebug ? 1 : 0);
    sentry_options_set_logger(sentry, &sdkLog, nullptr);

    QString caBundle;
#ifndef Q_OS_WIN
    caBundle = findCaBundle();
    if (!caBundle.isEmpty())
        sentry_options_set_ca_certs(sentry, caBundle.toUtf8().constData());
    else
        qCWarning(lcSentry) << "no CA certificate bundle found: sending to Sentry may fail";
#endif

    qCInfo(lcSentry).noquote() << QStringLiteral("starting: release cloudmus-qt@%1, environment %2, server %3, "
                                                 "database %4, CA bundle %5, %6 unsent event(s) from earlier runs")
                                      .arg(options.release, QString::fromLatin1(environment),
                                          QUrl(options.sentryDsn).host(), database,
                                          caBundle.isEmpty() ? QStringLiteral("(library default)") : caBundle)
                                      .arg(countUnsentEvents(database));
    // Sends the events of earlier runs in the background; failures are
    // logged by the SDK's transport as warnings.
    const int result = sentry_init(sentry);
    if (result == 0) {
        g_sentryActive.store(true, std::memory_order_release);
        qCInfo(lcSentry) << "started; a crash is stored and sent at the next start";
    } else {
        qCWarning(lcSentry) << "sentry_init failed with code" << result << "- crashes stay local";
    }
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
    if (!options.sentryDsn.isEmpty()) {
        startSentry(options);
    } else {
        // Switched off: events stored earlier must not be sent by a later
        // run that has it on again.
        const bool removed = QDir(options.reportDir + QStringLiteral("/sentry")).removeRecursively();
        qCInfo(lcSentry) << "off:" << (removed ? "events stored earlier were deleted" : "nothing stored");
    }
#else
    if (!options.sentryDsn.isEmpty())
        qCInfo(lcSentry) << "off: this build has no sentry-native";
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
    if (g_sentryActive.exchange(false)) {
        qCInfo(lcSentry) << "closing: waiting for what is still being sent";
        sentry_close();
        qCInfo(lcSentry) << "closed";
    }
#endif
}

void addBreadcrumb(const QString& message)
{
    const std::uint64_t index = g_breadcrumbNext.fetch_add(1, std::memory_order_relaxed);
    copyTruncated(g_breadcrumbs[index % kBreadcrumbCount], kBreadcrumbBytes, message.toUtf8());
#ifdef CLOUDMUS_HAS_SENTRY
    if (g_sentryActive.load(std::memory_order_acquire) && !t_inSdkLog)
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
