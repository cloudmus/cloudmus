#include "Debounce.h"

namespace Timing {

Debounce::Debounce(int delayMs, std::function<void()> action, QObject* parent)
    : QObject(parent)
    , action_(std::move(action))
{
    timer_.setSingleShot(true);
    timer_.setInterval(delayMs);
    connect(&timer_, &QTimer::timeout, this, [this]() { action_(); });
}

void Debounce::trigger() { timer_.start(); }

void Debounce::cancel() { timer_.stop(); }

void Debounce::flush()
{
    if (!timer_.isActive())
        return;
    timer_.stop();
    action_();
}

} // namespace Timing
