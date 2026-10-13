#include "Throttle.h"

#include <utility>

namespace Timing {

Throttle::Throttle(int intervalMs, std::function<void()> action, QObject* parent)
    : QObject(parent)
    , action_(std::move(action))
{
    timer_.setSingleShot(true);
    timer_.setInterval(intervalMs);
    connect(&timer_, &QTimer::timeout, this, [this]() {
        if (std::exchange(pending_, false))
            run();
    });
}

void Throttle::trigger()
{
    if (timer_.isActive())
        pending_ = true;
    else
        run();
}

void Throttle::cancel()
{
    timer_.stop();
    pending_ = false;
}

void Throttle::run()
{
    // Before the action: it may well trigger() again.
    timer_.start();
    action_();
}

} // namespace Timing
