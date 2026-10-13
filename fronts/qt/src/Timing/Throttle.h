#pragma once

#include <QObject>
#include <QTimer>

#include <functional>

namespace Timing {

// Runs an action at most once per `intervalMs`: the first trigger() runs
// it at once, the ones that come before the interval is over are folded
// into a single run at its end. So a steady stream of them (download
// progress) is shown at a fixed rate, and the last one is never lost.
class Throttle : public QObject {
public:
    Throttle(int intervalMs, std::function<void()> action, QObject* parent = nullptr);

    void trigger();
    // Forgets a run that is waiting; the next trigger() runs at once.
    void cancel();

private:
    void run();

    QTimer timer_;
    bool pending_ = false;
    std::function<void()> action_;
};

} // namespace Timing
