// Windows crash handling: the unhandled-exception filter (access
// violations and the like, from any thread, libmpv's included) and the
// CRT's SIGABRT. The stack is unwound with the x64 unwind tables every
// module carries (.pdata, emitted by MinGW too), so no debug symbols or
// dbghelp are needed for it; frames read as module+offset, which
// `addr2line -e <module>` resolves against an unstripped build. A
// minidump from dbghelp (loaded up front) goes next to the report.

#include <windows.h>

#include <dbghelp.h>

#include <csignal>

#include "CrashReporterDetail.h"

namespace Diagnostics::CrashReporter::Detail {

namespace {

constexpr int kMaxFrames = 64;

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
    PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

wchar_t g_reportDir[1024];
HANDLE g_file = INVALID_HANDLE_VALUE;
std::uint64_t g_reportTime = 0;
MiniDumpWriteDumpFn g_miniDumpWriteDump = nullptr;
LONG g_handling = 0;

// <reportDir>\crash-<unixTime><suffix>
bool reportPath(wchar_t* path, std::size_t size, std::uint64_t unixTime, const wchar_t* suffix)
{
    std::size_t length = 0;
    const auto append = [&](const wchar_t* text) {
        while (*text != L'\0' && length + 1 < size)
            path[length++] = *text++;
    };
    append(g_reportDir);
    append(L"\\crash-");
    wchar_t digits[21];
    std::size_t position = 20;
    digits[20] = L'\0';
    do {
        digits[--position] = wchar_t(L'0' + unixTime % 10);
        unixTime /= 10;
    } while (unixTime != 0);
    append(digits + position);
    append(suffix);
    path[length] = L'\0';
    return length + 1 < size;
}

// module+offset, or the bare address outside any module.
void writeLocation(DWORD64 address)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module)) {
        wchar_t wide[MAX_PATH];
        const DWORD length = GetModuleFileNameW(module, wide, MAX_PATH);
        const wchar_t* name = wide;
        for (DWORD i = 0; i < length; ++i) {
            if (wide[i] == L'\\' || wide[i] == L'/')
                name = wide + i + 1;
        }
        char utf8[MAX_PATH * 3];
        const int bytes = WideCharToMultiByte(
            CP_UTF8, 0, name, int(length - (name - wide)), utf8, int(sizeof utf8), nullptr, nullptr);
        write(utf8, bytes > 0 ? std::size_t(bytes) : 0);
        write("+");
        writeHex(address - reinterpret_cast<DWORD64>(module));
    } else {
        writeHex(address);
    }
}

void writeFrame(int index, DWORD64 address)
{
    write("  #");
    writeDecimal(std::uint64_t(index));
    write(" ");
    writeLocation(address);
    write("\n");
}

bool readable(DWORD64 address)
{
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof info) == 0)
        return false;
    const DWORD readableProtection = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ
        | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return info.State == MEM_COMMIT && (info.Protect & readableProtection) != 0 && (info.Protect & PAGE_GUARD) == 0;
}

void writeStack(CONTEXT context)
{
    for (int i = 0; i < kMaxFrames && context.Rip != 0; ++i) {
        writeFrame(i, context.Rip);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (function != nullptr) {
            PVOID handlerData = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData,
                &establisherFrame, nullptr);
        } else {
            // A leaf function (no unwind entry): the return address is on
            // top of the stack.
            if (!readable(context.Rsp))
                break;
            context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
            context.Rsp += 8;
        }
    }
}

const char* exceptionName(DWORD code)
{
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "access violation";
        case EXCEPTION_STACK_OVERFLOW:
            return "stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "integer divide by zero";
        case EXCEPTION_IN_PAGE_ERROR:
            return "in-page error";
        case EXCEPTION_BREAKPOINT:
            return "breakpoint";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "privileged instruction";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "datatype misalignment";
    }
    return "exception";
}

void writeMiniDump(EXCEPTION_POINTERS* exception)
{
    if (g_miniDumpWriteDump == nullptr)
        return;
    wchar_t path[1100];
    if (!reportPath(path, 1100, g_reportTime, L".dmp"))
        return;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    MINIDUMP_EXCEPTION_INFORMATION info { };
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;
    g_miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, exception ? &info : nullptr,
        nullptr, nullptr);
    CloseHandle(file);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* exception)
{
    // Only the first crashing thread reports.
    if (InterlockedExchange(&g_handling, 1) != 0)
        return EXCEPTION_CONTINUE_SEARCH;
    const EXCEPTION_RECORD& record = *exception->ExceptionRecord;
    if (beginReport(unixTimeNow())) {
        writeHeader();
        write("reason: ");
        write(exceptionName(record.ExceptionCode));
        write(" (");
        writeHex(record.ExceptionCode);
        write(") at ");
        writeLocation(reinterpret_cast<DWORD64>(record.ExceptionAddress));
        write("\n");
        if ((record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
            && record.NumberParameters >= 2) {
            const ULONG_PTR operation = record.ExceptionInformation[0];
            write(operation == 0 ? "reading " : operation == 8 ? "executing " : "writing ");
            writeHex(record.ExceptionInformation[1]);
            write("\n");
        }
        write("thread: ");
        writeDecimal(GetCurrentThreadId());
        write("\n");
        // The dump before the stack walk: a corrupt stack can make the
        // walk itself fault, and the dump is the more complete of the two.
        writeMiniDump(exception);
        write("\nstack:\n");
        writeStack(*exception->ContextRecord);
        writeBreadcrumbs();
        endReport();
    }
    // Windows Error Reporting still sees the crash, and the process ends
    // as it would have.
    return EXCEPTION_CONTINUE_SEARCH;
}

void onAbort(int)
{
    if (InterlockedExchange(&g_handling, 1) == 0 && beginReport(unixTimeNow())) {
        writeHeader();
        write("reason: abort()\n\nstack:\n");
        writeCurrentStack();
        writeBreadcrumbs();
        endReport();
    }
    // Returning ends the process: the CRT terminates after the handler.
}

} // namespace

void installPlatformHandlers()
{
    MultiByteToWideChar(CP_UTF8, 0, reportDirUtf8(), -1, g_reportDir, int(sizeof g_reportDir / sizeof(wchar_t)));
    if (HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll"))
        g_miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(
            reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
    SetUnhandledExceptionFilter(onUnhandledException);
    std::signal(SIGABRT, onAbort);
}

bool beginReport(std::uint64_t unixTime)
{
    wchar_t path[1100];
    if (!reportPath(path, 1100, unixTime, L".txt"))
        return false;
    g_reportTime = unixTime;
    g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return g_file != INVALID_HANDLE_VALUE;
}

void write(const char* text, std::size_t length)
{
    if (g_file == INVALID_HANDLE_VALUE || length == 0)
        return;
    DWORD written = 0;
    WriteFile(g_file, text, DWORD(length), &written, nullptr);
}

void endReport()
{
    if (g_file != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_file);
        CloseHandle(g_file);
    }
    g_file = INVALID_HANDLE_VALUE;
}

void writeCurrentStack()
{
    CONTEXT context { };
    RtlCaptureContext(&context);
    writeStack(context);
}

void disarmAbortHandler()
{
    InterlockedExchange(&g_handling, 1);
    std::signal(SIGABRT, SIG_DFL);
}

std::uint64_t unixTimeNow()
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const std::uint64_t ticks = (std::uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    // FILETIME counts 100 ns ticks since 1601-01-01.
    return (ticks - 116444736000000000ULL) / 10000000ULL;
}

} // namespace Diagnostics::CrashReporter::Detail
