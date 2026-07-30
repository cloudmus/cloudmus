#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>

namespace Rpc {

// One JSON value per line, compact serialization, UTF-8 — see
// docs/protocol.md §2. Owns the backend subprocess; NOT a coroutine itself
// (it's a push source: lines can arrive at any time), sitting below
// RpcClient's request/response correlation layer.
class NdjsonTransport : public QObject {
    Q_OBJECT

public:
    static constexpr int kStderrTailLines = 50;

    explicit NdjsonTransport(QObject* parent = nullptr);

    void start(const QStringList& argv);
    void writeMessage(const QJsonObject& message);

    // Graceful shutdown: close stdin (backend's read loop sees EOF), then
    // SIGTERM after a grace period, then SIGKILL as a last resort — see
    // docs/protocol.md §4.2. Safe to call more than once.
    void closeStdin();
    void terminateThenKill(int gracePeriodMs = 2000);

    QProcess::ProcessState state() const;
    QStringList stderrTail() const;

signals:
    void messageReceived(QJsonObject message);
    void framingError(QString rawLine);
    void finished(int exitCode, QProcess::ExitStatus status);
    void errorOccurred(QProcess::ProcessError error);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();

private:
    QProcess process_;
    QByteArray stdoutBuffer_;
    QList<QByteArray> stderrTail_;
    // QProcess::start() is asynchronous — the process may still be
    // QProcess::Starting when writeMessage() is first called (RpcClient::
    // start() writes `initialize` right after calling start(), with no
    // suspension point in between). Writes issued before the process
    // reaches Running are queued here and flushed on QProcess::started,
    // instead of being silently dropped.
    QList<QByteArray> pendingWrites_;
};

} // namespace Rpc
