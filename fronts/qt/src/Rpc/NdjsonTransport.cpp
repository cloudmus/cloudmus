#include "NdjsonTransport.h"

#include <QJsonDocument>
#include <QLoggingCategory>

namespace Rpc {

namespace {
Q_LOGGING_CATEGORY(lcTransport, "cloudmus.rpc.transport")
}

NdjsonTransport::NdjsonTransport(QObject* parent)
    : QObject(parent)
{
    connect(&process_, &QProcess::readyReadStandardOutput, this, &NdjsonTransport::onReadyReadStdout);
    connect(&process_, &QProcess::readyReadStandardError, this, &NdjsonTransport::onReadyReadStderr);
    connect(&process_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) { emit finished(exitCode, status); });
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
}

void NdjsonTransport::start(const QStringList& argv)
{
    if (argv.isEmpty())
        return;
    qCDebug(lcTransport) << "spawning" << argv;
    process_.setProgram(argv.first());
    process_.setArguments(argv.mid(1));
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
        if (line.isEmpty())
            continue;
        stderrTail_.append(line);
        while (stderrTail_.size() > kStderrTailLines) {
            stderrTail_.removeFirst();
        }
        emit stderrLine(QString::fromUtf8(line));
    }
}

} // namespace Rpc
