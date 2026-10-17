#include "QuitSignals.h"

#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Integration {

namespace {

constexpr int kSignals[] = { SIGTERM, SIGINT, SIGHUP };

// The usual self-pipe: write() is all a handler may safely do, the event
// loop reads the other end.
int g_fds[2] = { -1, -1 };

void onSignal(int signal)
{
    const int savedErrno = errno;
    const unsigned char byte = static_cast<unsigned char>(signal);
    [[maybe_unused]] const ssize_t written = ::write(g_fds[0], &byte, 1);
    errno = savedErrno;
}

} // namespace

QuitSignals::QuitSignals(QObject* parent)
    : QObject(parent)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, g_fds) != 0)
        return;
    // Never block in the handler, even with the buffer full of signals
    // nobody has read yet.
    ::fcntl(g_fds[0], F_SETFL, ::fcntl(g_fds[0], F_GETFL) | O_NONBLOCK);
    notifier_ = new QSocketNotifier(g_fds[1], QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &QuitSignals::onReadable);

    struct sigaction action = { };
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (int signal : kSignals)
        sigaction(signal, &action, nullptr);
}

QuitSignals::~QuitSignals()
{
    struct sigaction action = { };
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    for (int signal : kSignals)
        sigaction(signal, &action, nullptr);
    if (g_fds[0] >= 0) {
        ::close(g_fds[0]);
        ::close(g_fds[1]);
        g_fds[0] = g_fds[1] = -1;
    }
}

void QuitSignals::onReadable()
{
    unsigned char byte = 0;
    if (::read(g_fds[1], &byte, 1) == 1)
        emit received(byte);
}

} // namespace Integration
