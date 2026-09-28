// Linux (POSIX) crash handling: fatal signals, on an alternate stack so a
// stack overflow is caught too. The stack comes from glibc's backtrace(),
// written straight to the report with backtrace_symbols_fd(), which doesn't
// allocate. Symbols read as module(function+offset) for exported ones and
// module(+offset) otherwise — `addr2line -e <module>` resolves the rest.

#include <csignal>
#include <cstring>
#include <ctime>

#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

#include "CrashReporterDetail.h"

namespace Diagnostics::CrashReporter::Detail {

namespace {

constexpr int kSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
constexpr int kMaxFrames = 64;

int g_fd = -1;
char g_altStack[64 * 1024];

const char* signalName(int signal)
{
    switch (signal) {
        case SIGSEGV:
            return "SIGSEGV";
        case SIGBUS:
            return "SIGBUS";
        case SIGILL:
            return "SIGILL";
        case SIGFPE:
            return "SIGFPE";
        case SIGABRT:
            return "SIGABRT";
    }
    return "signal";
}

void onSignal(int signal, siginfo_t* info, void*)
{
    // Only the first crashing thread writes; any other one just waits for
    // the re-raise below to end the process.
    static volatile sig_atomic_t handling = 0;
    if (handling == 0) {
        handling = 1;
        if (beginReport(unixTimeNow())) {
            writeHeader();
            write("reason: ");
            write(signalName(signal));
            write(" (");
            writeDecimal(std::uint64_t(signal));
            write(")");
            // si_addr means the faulting address only when the kernel sent
            // the signal (a kill() fills it with the sender's pid/uid).
            if (signal != SIGABRT && info->si_code > 0) {
                write(" at ");
                writeHex(reinterpret_cast<std::uintptr_t>(info->si_addr));
            }
            write("\n\nstack:\n");
            writeCurrentStack();
            writeBreadcrumbs();
            endReport();
        }
        static const char notice[] = "cloudmus-qt: crashed, report written to the crashes directory\n";
        [[maybe_unused]] ssize_t ignored = ::write(STDERR_FILENO, notice, sizeof notice - 1);
    }
    // Die the way the signal would have without us: the exit status and a
    // core dump stay as they were.
    struct sigaction fallback { };
    fallback.sa_handler = SIG_DFL;
    sigemptyset(&fallback.sa_mask);
    sigaction(signal, &fallback, nullptr);
    raise(signal);
}

} // namespace

void installPlatformHandlers()
{
    // backtrace() loads libgcc on first use, which allocates — do that now
    // rather than inside a crash.
    void* warmUp[1];
    backtrace(warmUp, 1);

    stack_t stack { };
    stack.ss_sp = g_altStack;
    stack.ss_size = sizeof g_altStack;
    sigaltstack(&stack, nullptr);

    struct sigaction action { };
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal : kSignals)
        sigaction(signal, &action, nullptr);
}

bool beginReport(std::uint64_t unixTime)
{
    char path[1200];
    const char* dir = reportDirUtf8();
    std::size_t length = strnlen(dir, 1024);
    std::memcpy(path, dir, length);
    const char prefix[] = "/crash-";
    std::memcpy(path + length, prefix, sizeof prefix - 1);
    length += sizeof prefix - 1;
    char digits[21];
    std::size_t position = sizeof digits;
    do {
        digits[--position] = char('0' + unixTime % 10);
        unixTime /= 10;
    } while (unixTime != 0);
    std::memcpy(path + length, digits + position, sizeof digits - position);
    length += sizeof digits - position;
    std::memcpy(path + length, ".txt", 5);

    g_fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    return g_fd >= 0;
}

void write(const char* text, std::size_t length)
{
    while (g_fd >= 0 && length > 0) {
        const ssize_t written = ::write(g_fd, text, length);
        if (written <= 0)
            return;
        text += written;
        length -= std::size_t(written);
    }
}

void endReport()
{
    if (g_fd >= 0)
        ::close(g_fd);
    g_fd = -1;
}

void writeCurrentStack()
{
    void* frames[kMaxFrames];
    const int count = backtrace(frames, kMaxFrames);
    if (g_fd >= 0)
        backtrace_symbols_fd(frames, count, g_fd);
}

void disarmAbortHandler()
{
    struct sigaction fallback { };
    fallback.sa_handler = SIG_DFL;
    sigemptyset(&fallback.sa_mask);
    sigaction(SIGABRT, &fallback, nullptr);
}

std::uint64_t unixTimeNow() { return std::uint64_t(std::time(nullptr)); }

} // namespace Diagnostics::CrashReporter::Detail
