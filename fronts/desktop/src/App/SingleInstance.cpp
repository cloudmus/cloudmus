#include "SingleInstance.h"

#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace App {

namespace {

Q_LOGGING_CATEGORY(lcSingle, "cloudmus.app.single")

// How long a launch keeps trying to reach the running instance: it may still
// be starting up (lock taken, socket not yet listening) or shutting down.
constexpr int kHandoverTimeoutMs = 3000;
constexpr int kRetryPauseMs = 100;
// A launch is a line of arguments; anything bigger isn't one.
constexpr qint64 kMaxMessageBytes = 1024 * 1024;

QString runtimeDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir;
}

QString socketNameFor(const QString& key)
{
#ifdef Q_OS_WIN
    // Pipe names are shared by the whole machine, unlike a socket in the
    // user's own runtime directory.
    return key + u'-' + qEnvironmentVariable("USERNAME");
#else
    return runtimeDir() + u'/' + key + QStringLiteral(".sock");
#endif
}

} // namespace

SingleInstance::SingleInstance(const QString& key, QObject* parent)
    : QObject(parent)
    , serverName_(socketNameFor(key))
    , lock_(runtimeDir() + u'/' + key + QStringLiteral(".lock"))
{
    // Only a dead owner makes the lock stale, never its age: the app runs
    // for days.
    lock_.setStaleLockTime(0);
}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::claim(const Launch& launch)
{
    if (lock_.tryLock(0)) {
        listen();
        return true;
    }
    if (lock_.error() != QLockFile::LockFailedError) {
        // Can't lock at all (a read-only runtime directory): better a second
        // instance than none.
        qCWarning(lcSingle) << "can't take the instance lock:" << lock_.fileName() << lock_.error();
        return true;
    }

    QElapsedTimer waited;
    waited.start();
    do {
        if (send(launch))
            return false;
        QThread::msleep(kRetryPauseMs);
        // The owner may have died meanwhile.
        if (lock_.tryLock(0)) {
            listen();
            return true;
        }
    } while (waited.elapsed() < kHandoverTimeoutMs);
    qCWarning(lcSingle) << "another instance holds the lock but doesn't answer";
    return false;
}

bool SingleInstance::send(const Launch& launch)
{
#ifdef Q_OS_WIN
    // This process was just started by the user's click and so may bring
    // windows up; Windows lets the running instance do it only if told so.
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    QLocalSocket socket;
    socket.connectToServer(serverName_);
    if (!socket.waitForConnected(500))
        return false;
    socket.write(encode(launch));
    if (!socket.waitForBytesWritten(1000))
        return false;
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
        socket.waitForDisconnected(500);
    return true;
}

void SingleInstance::listen()
{
    server_ = std::make_unique<QLocalServer>();
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    // The lock is ours, so a socket file still there is a crashed run's.
    QLocalServer::removeServer(serverName_);
    if (!server_->listen(serverName_)) {
        qCWarning(lcSingle) << "can't listen on" << serverName_ << server_->errorString();
        server_.reset();
        return;
    }
    connect(server_.get(), &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket* socket = server_->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                if (socket->bytesAvailable() > kMaxMessageBytes) {
                    socket->abort();
                    return;
                }
                if (!socket->canReadLine())
                    return;
                const Launch launch = decode(socket->readLine());
                socket->disconnectFromServer();
                emit launched(launch);
            });
        }
    });
}

QByteArray SingleInstance::encode(const Launch& launch)
{
    const QJsonObject object {
        { QStringLiteral("arguments"), QJsonArray::fromStringList(launch.arguments) },
        { QStringLiteral("workingDirectory"), launch.workingDirectory },
        { QStringLiteral("activationToken"), launch.activationToken },
    };
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

SingleInstance::Launch SingleInstance::decode(const QByteArray& line)
{
    // Unknown fields are skipped and missing ones left empty, so a newer
    // launch can still be read by an older running instance and back.
    const QJsonObject object = QJsonDocument::fromJson(line).object();
    Launch launch;
    for (const QJsonValue& argument : object.value(QStringLiteral("arguments")).toArray())
        launch.arguments.append(argument.toString());
    launch.workingDirectory = object.value(QStringLiteral("workingDirectory")).toString();
    launch.activationToken = object.value(QStringLiteral("activationToken")).toString();
    return launch;
}

} // namespace App
