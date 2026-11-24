#pragma once

#include <cstddef>
#include <cstdint>

// The seam between CrashReporter.cpp (buffers, report layout, terminate
// handler) and one platform's crash handling (CrashReporterPosix.cpp or
// CrashReporterWin.cpp). Everything here runs inside a crash handler:
// no allocation, no Qt, no locks.
namespace Diagnostics::CrashReporter::Detail {

// Called once from install(), after the common buffers are filled.
void installPlatformHandlers();

// Opens a new report file (<reportDir>/crash-<unixTime>.txt); false when
// it can't be created. write() goes to it until endReport().
bool beginReport(std::uint64_t unixTime);
void write(const char* text, std::size_t length);
void endReport();

// The stack of the calling thread, one frame per line (module+offset).
void writeCurrentStack();

// Before std::abort() from the terminate handler: the report is already
// written, so the abort itself must not produce a second one.
void disarmAbortHandler();

std::uint64_t unixTimeNow();

// From CrashReporter.cpp, for the platform code.
void write(const char* text);
void writeDecimal(std::uint64_t value);
void writeHex(std::uint64_t value);
// Release, OS, report time; the reason line comes next.
void writeHeader();
// Last log lines; ends the report body.
void writeBreadcrumbs();
const char* reportDirUtf8();

} // namespace Diagnostics::CrashReporter::Detail
