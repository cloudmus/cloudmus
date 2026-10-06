#include "NdjsonTransport.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QTemporaryFile>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Rpc {

namespace {
Q_LOGGING_CATEGORY(lcTransport, "cloudmus.rpc.transport")

constexpr int kStderrFilePollMs = 200;

bool runningUnderWine()
{
#ifdef Q_OS_WIN
    static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    return wine;
#else
    return false;
#endif
}
}

NdjsonTransport::NdjsonTransport(QObject* parent)
    : QObject(parent)
{
    connect(&process_, &QProcess::readyReadStandardOutput, this, &NdjsonTransport::onReadyReadStdout);
    connect(&process_, &QProcess::readyReadStandardError, this, &NdjsonTransport::onReadyReadStderr);
    connect(&process_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        // The tail must be complete before anyone reads stderrTail().
        readStderrFile();
        stderrFileTimer_.stop();
        stderrFile_.reset();
        if (!stderrFilePath_.isEmpty())
            QFile::remove(stderrFilePath_);
        stderrFilePath_.clear();
        emit finished(exitCode, status);
    });
    connect(&process_, &QProcess::errorOccurred, this, &NdjsonTransport::errorOccurred);
    // QProcess::start() is asynchronous: writeMessage() can be (and is,
    // for the very first `initialize` request — see RpcClient::start())
    // called before the process has actually reached the Running state.
    // Flush anything queued in the meantime once it does.
    connect(&process_, &QProcess::started, this, [this]() {
        qCDebug(lcTransport) << "process started, flushing" << pendingWrites_.size() << "queued write(s)";
        for (const QByteArray& line : pendingWrites_) {
            process_.write(line);
        }
        pendingWrites_.clear();
    });
    stderrFileTimer_.setInterval(kStderrFilePollMs);
    connect(&stderrFileTimer_, &QTimer::timeout, this, &NdjsonTransport::readStderrFile);
}

NdjsonTransport::~NdjsonTransport()
{
    stderrFile_.reset();
    if (!stderrFilePath_.isEmpty())
        QFile::remove(stderrFilePath_);
}

void NdjsonTransport::start(const QStringList& argv, const QProcessEnvironment& environment)
{
    if (argv.isEmpty())
        return;
    qCDebug(lcTransport) << "spawning" << argv;
    process_.setProgram(argv.first());
    process_.setArguments(argv.mid(1));
    process_.setProcessEnvironment(environment);
#ifdef Q_OS_WIN
    // No console at all: CREATE_NO_WINDOW still gives each backend a hidden
    // one, i.e. a conhost.exe per backend. Backends talk over the pipes
    // QProcess sets up and start no console programs (one started from a
    // detached process would get a visible console window).
    process_.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* args) { args->flags |= DETACHED_PROCESS; });
#endif
    // Under Wine, QProcess loses stdout data when the backend also writes to
    // stderr: after an idle pause the reply to a request never arrives (every
    // request "times out", even local-folder's), while stderr keeps coming.
    // Reproduced with a bare QProcess and a ten-line Python child, on Wine
    // 9.0 and 11.19; never on real Windows, and never once the child's stderr
    // isn't a pipe. So there it goes to a file, read on a timer.
    stderrFile_.reset();
    stderrPartialLine_.clear();
    if (!stderrFilePath_.isEmpty())
        QFile::remove(stderrFilePath_);
    stderrFilePath_.clear();
    if (runningUnderWine()) {
        QTemporaryFile file(QDir::tempPath() + QStringLiteral("/cloudmus-backend-stderr-XXXXXX.log"));
        file.setAutoRemove(false);
        if (file.open()) {
            stderrFilePath_ = file.fileName();
            file.close();
            stderrFile_ = std::make_unique<QFile>(stderrFilePath_);
            if (stderrFile_->open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
                process_.setStandardErrorFile(stderrFilePath_);
                stderrFileTimer_.start();
            } else {
                stderrFile_.reset();
            }
        }
    }
    process_.start();
}

void NdjsonTransport::writeMessage(const QJsonObject& message)
{
    QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
    line.append('\n');
    switch (process_.state()) {
        case QProcess::Running:
            process_.write(line);
            break;
        case QProcess::Starting:
            qCDebug(lcTransport) << "process still starting, queueing write";
            pendingWrites_.append(line);
            break;
        case QProcess::NotRunning:
            qCWarning(lcTransport) << "writeMessage() called with no process running — dropped";
            break;
    }
}

void NdjsonTransport::closeStdin()
{
    if (process_.state() == QProcess::Running) {
        process_.closeWriteChannel();
    }
}

void NdjsonTransport::terminateThenKill(int gracePeriodMs)
{
    if (process_.state() != QProcess::Running)
        return;
    process_.terminate();
    if (!process_.waitForFinished(gracePeriodMs)) {
        process_.kill();
    }
}

QProcess::ProcessState NdjsonTransport::state() const { return process_.state(); }

QStringList NdjsonTransport::stderrTail() const
{
    QStringList out;
    out.reserve(stderrTail_.size());
    for (const QByteArray& line : stderrTail_) {
        out.append(QString::fromUtf8(line));
    }
    return out;
}

void NdjsonTransport::onReadyReadStdout()
{
    stdoutBuffer_.append(process_.readAllStandardOutput());
    for (;;) {
        int newlineIndex = stdoutBuffer_.indexOf('\n');
        if (newlineIndex < 0)
            break;
        QByteArray line = stdoutBuffer_.left(newlineIndex);
        stdoutBuffer_.remove(0, newlineIndex + 1);
        if (line.trimmed().isEmpty())
            continue; // readers tolerate blank lines, see docs/protocol.md §2

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit framingError(QString::fromUtf8(line));
            continue;
        }
        emit messageReceived(doc.object());
    }
}

void NdjsonTransport::onReadyReadStderr()
{
    QByteArray chunk = process_.readAllStandardError();
    const QList<QByteArray> lines = chunk.split('\n');
    for (const QByteArray& line : lines) {
        if (!line.isEmpty())
            handleStderrLine(line);
    }
}

void NdjsonTransport::readStderrFile()
{
    if (!stderrFile_)
        return;
    stderrPartialLine_.append(stderrFile_->readAll());
    qsizetype newline;
    while ((newline = stderrPartialLine_.indexOf('\n')) >= 0) {
        QByteArray line = stderrPartialLine_.left(newline);
        stderrPartialLine_.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (!line.trimmed().isEmpty())
            handleStderrLine(line);
    }
}

void NdjsonTransport::handleStderrLine(const QByteArray& line)
{
    stderrTail_.append(line);
    while (stderrTail_.size() > kStderrTailLines) {
        stderrTail_.removeFirst();
    }
    emit stderrLine(QString::fromUtf8(line));
}

} // namespace Rpc
