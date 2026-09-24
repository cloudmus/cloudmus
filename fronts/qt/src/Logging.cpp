#include "Logging.h"

#include <cstdio>
#include <cstdlib>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>

namespace {

bool g_debugEnabled = false;
QFile* g_logFile = nullptr;

const char* levelName(QtMsgType type)
{
    switch (type) {
        case QtDebugMsg:
            return "DEBUG";
        case QtInfoMsg:
            return "INFO";
        case QtWarningMsg:
            return "WARN";
        case QtCriticalMsg:
            return "ERROR";
        case QtFatalMsg:
            return "FATAL";
    }
    return "?";
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Verbose (qDebug/qInfo) output is opt-in; warnings/errors always show —
    // those indicate a real problem (backend spawn failure, RPC error, etc.)
    // the user should see without needing --debug.
    if ((type == QtDebugMsg || type == QtInfoMsg) && !g_debugEnabled)
        return;

    // context.category is "default" for plain qDebug()/qWarning() calls (no
    // QLoggingCategory involved) — only show it when it's an actual named
    // category (e.g. "cloudmus.rpc.client", declared once per module via
    // Q_LOGGING_CATEGORY — see Rpc/RpcClient.cpp — instead of repeating the
    // module name as a string literal in every log call).
    const bool hasCategory = context.category != nullptr && qstrcmp(context.category, "default") != 0;
    const QString categoryPrefix
        = hasCategory ? QString::fromLatin1(context.category) + QStringLiteral(": ") : QString();

    const QString line = QStringLiteral("%1 [%2] %3%4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QString::fromLatin1(levelName(type)), categoryPrefix, msg);

    FILE* stream = (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ? stderr : stdout;
    std::fprintf(stream, "%s\n", qPrintable(line));
    std::fflush(stream);

    if (g_logFile != nullptr && g_logFile->isOpen()) {
        QTextStream ts(g_logFile);
        ts << line << '\n';
        ts.flush();
    }

    if (type == QtFatalMsg)
        std::abort();
}

} // namespace

bool debugLoggingRequested()
{
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLOUDMUS_QT_DEBUG"));
    const QString normalized = env.trimmed().toLower();
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("true")
        || normalized == QStringLiteral("yes")) {
        return true;
    }
    return QCoreApplication::arguments().contains(QStringLiteral("--debug"));
}

void installLogging()
{
    g_debugEnabled = debugLoggingRequested();
    if (g_debugEnabled) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/cloudmus/fronts/qt");
        QDir().mkpath(dir);
        g_logFile = new QFile(dir + QStringLiteral("/debug.log"));
        if (!g_logFile->open(QIODevice::Append | QIODevice::Text)) {
            delete g_logFile;
            g_logFile = nullptr;
        }
    }
    qInstallMessageHandler(messageHandler);
    if (g_debugEnabled) {
        qInfo() << "cloudmus-qt: debug logging enabled"
                << (g_logFile != nullptr ? "(also writing to " + g_logFile->fileName() + ")"
                                         : "(log file unavailable)");
    }
}
