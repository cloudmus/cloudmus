#pragma once

#include <QString>

// One log for every layer of the mobile front, in the format the Java side
// writes on Android (dev.cloudmus.mobile.CmLog), so Ui::LogModel reads both:
//
//   2026-10-31 12:45:01.123 [   12.345] playback/main I Tag: message
//
// The bracket is seconds since the system booted (deep sleep included):
// what tells how long after the car was started something happened.
namespace Logging {

// Routes qDebug()/qWarning()/... of this process into the log. `process`
// names the process in each line and in the file name ("ui", "playback").
// On Android the lines go to CmLog over JNI — it owns the files there;
// elsewhere they are written to `dir` (created if missing).
void install(const QString& process, const QString& dir);

// A startup milestone: logged with the time since the process started, and
// on Android mirrored to logcat under the CloudmusStart tag.
void mark(const QString& what);

// Where install() writes on the desktop (empty on Android).
QString directory();

} // namespace Logging
