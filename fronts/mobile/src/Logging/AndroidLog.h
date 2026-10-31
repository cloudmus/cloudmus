#pragma once

#include <QString>

// Hands log lines to dev.cloudmus.mobile.CmLog, which writes the files on
// Android. Plain JNI, not QJniObject: the playback process runs Qt Core
// without Qt's Android bootstrap, so this mustn't rely on it.
namespace Logging::AndroidLog {

void write(char level, const QString& tag, const QString& message);
void mark(const QString& what);

} // namespace Logging::AndroidLog
