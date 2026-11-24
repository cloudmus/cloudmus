#pragma once

#include <QObject>
#include <QTimer>

#include <functional>

namespace Timing {

// Runs an action once things have gone quiet: every trigger() puts it off
// by another `delayMs`, so a burst of them (a drag, typing) ends in one
// run after the last.
class Debounce : public QObject {
public:
    Debounce(int delayMs, std::function<void()> action, QObject* parent = nullptr);

    void trigger();
    // Forgets a run that is waiting.
    void cancel();
    // Does a waiting run now rather than later; nothing if none is.
    void flush();
    bool isPending() const { return timer_.isActive(); }

private:
    QTimer timer_;
    std::function<void()> action_;
};

} // namespace Timing
