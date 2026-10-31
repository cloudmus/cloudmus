#include "Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QThread>

#include <cstdio>
#include <ctime>

#ifdef Q_OS_ANDROID
#include "AndroidLog.h"
#endif

namespace Logging {

namespace {

QString processName;
QString logDir;
QMutex mutex;
QFile file;
QtMessageHandler previousHandler = nullptr;
qint64 processStartMs = 0;

qint64 bootClockMs()
{
    timespec ts { };
#ifdef CLOCK_BOOTTIME
    clock_gettime(CLOCK_BOOTTIME, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return qint64(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

char levelLetter(QtMsgType type)
{
    switch (type) {
        case QtDebugMsg:
            return 'D';
        case QtInfoMsg:
            return 'I';
        case QtWarningMsg:
            return 'W';
        case QtCriticalMsg:
            return 'E';
        case QtFatalMsg:
            return 'F';
    }
    return 'I';
}

QString threadName()
{
    QString name = QThread::currentThread()->objectName();
    if (name.isEmpty())
        name = (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
            ? QStringLiteral("main")
            : QString::number(quintptr(QThread::currentThreadId()), 16);
    return name.replace(QLatin1Char(' '), QLatin1Char('_'));
}

// Same layout as CmLog.format() on the Java side.
QString formatLine(char level, const QString& tag, const QString& message)
{
    QString text = message;
    // Continuation lines are indented: the reader glues them to the entry.
    text.replace(QLatin1Char('\n'), QStringLiteral("\n    "));
    return QStringLiteral("%1 [%2] %3/%4 %5 %6: %7\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
        .arg(bootClockMs() / 1000.0, 9, 'f', 3)
        .arg(processName, threadName(), QString(QLatin1Char(level)), tag, text);
}

void write(char level, const QString& tag, const QString& message)
{
#ifdef Q_OS_ANDROID
    AndroidLog::write(level, tag, message);
#else
    const QString line = formatLine(level, tag, message);
    const QByteArray utf8 = line.toUtf8();
    QMutexLocker lock(&mutex);
    if (file.isOpen()) {
        file.write(utf8);
        file.flush();
    }
    std::fwrite(utf8.constData(), 1, size_t(utf8.size()), stderr);
#endif
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString tag = context.category && qstrcmp(context.category, "default") != 0
        ? QString::fromLatin1(context.category)
        : QStringLiteral("qt");
    write(levelLetter(type), tag, message);
    if (type == QtFatalMsg && previousHandler)
        previousHandler(type, context, message);
}

} // namespace

void install(const QString& process, const QString& dir)
{
    processName = process;
    processStartMs = bootClockMs();
#ifndef Q_OS_ANDROID
    logDir = dir;
    QDir().mkpath(dir);
    file.setFileName(dir + QLatin1Char('/') + process + QLatin1Char('-')
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".log"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        std::fprintf(stderr, "can't open the log file %s\n", qPrintable(file.fileName()));
#else
    Q_UNUSED(dir)
#endif
    previousHandler = qInstallMessageHandler(handler);
}

void mark(const QString& what)
{
#ifdef Q_OS_ANDROID
    AndroidLog::mark(what);
#else
    write('I', QStringLiteral("Startup"),
        QStringLiteral("%1 (+%2 ms since process start)").arg(what).arg(bootClockMs() - processStartMs));
#endif
}

QString directory() { return logDir; }

} // namespace Logging
