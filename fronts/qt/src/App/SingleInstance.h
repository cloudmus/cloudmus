#pragma once

#include <QLockFile>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QLocalServer;

namespace App {

// Only one cloudmus-qt runs per user: a second one would start its own
// backends, tray icon and MPRIS player over the same config and cache. A
// lock file says who runs (a crashed run's lock goes stale with its PID); a
// local socket next to it carries a later launch over to the running one,
// which then exits.
//
// The whole launch is carried, not just "show yourself", so that the running
// instance can later act on its arguments — e.g. queue a track file opened
// from a file manager.
class SingleInstance : public QObject {
    Q_OBJECT

public:
    struct Launch {
        // Without the program name.
        QStringList arguments;
        // To resolve relative paths among the arguments.
        QString workingDirectory;
        // XDG activation (Wayland) or startup notification (X11) token the
        // launcher gave the new process, for the running one to raise its
        // window with.
        QString activationToken;
    };

    // `key` names the lock and the socket; tests pass their own.
    explicit SingleInstance(const QString& key = QStringLiteral("cloudmus-qt"), QObject* parent = nullptr);
    ~SingleInstance() override;

    // true: this is the one running instance — launched() reports later
    // ones. false: another one runs and was handed `launch` (best effort).
    bool claim(const Launch& launch);

    static QByteArray encode(const Launch& launch);
    static Launch decode(const QByteArray& line);

signals:
    void launched(const App::SingleInstance::Launch& launch);

private:
    bool send(const Launch& launch);
    void listen();

    QString serverName_;
    QLockFile lock_;
    std::unique_ptr<QLocalServer> server_;
};

} // namespace App
