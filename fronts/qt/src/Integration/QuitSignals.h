#pragma once

#include <QObject>

class QSocketNotifier;

namespace Integration {

// SIGTERM, SIGINT and SIGHUP (kill, Ctrl+C, a closed terminal, the session
// ending) as a Qt signal, so they quit the way the Quit menu item does —
// backends shut down first. Left at their default, they ended the process
// on the spot and a backend that didn't exit on its own outlived the app.
// POSIX only; one instance per process.
class QuitSignals : public QObject {
    Q_OBJECT

public:
    explicit QuitSignals(QObject* parent = nullptr);
    ~QuitSignals() override;

signals:
    // On the event loop, not in the signal handler.
    void received(int signal);

private:
    void onReadable();

    QSocketNotifier* notifier_ = nullptr;
};

} // namespace Integration
