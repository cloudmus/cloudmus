#include "FrameClock.h"

#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

namespace Ui {

namespace {
constexpr qreal kMinRefreshHz = 60.0; // never slower than Qt's own default
constexpr qreal kMaxRefreshHz = 500.0; // guards against a bogus reported rate
} // namespace

std::chrono::nanoseconds displayFrameInterval()
{
    qreal hz = kMinRefreshHz;
    for (const QScreen* screen : QGuiApplication::screens())
        hz = std::max(hz, screen->refreshRate());
    hz = std::min(hz, kMaxRefreshHz);
    return std::chrono::nanoseconds(static_cast<qint64>(1e9 / hz));
}

DisplayAnimationDriver::DisplayAnimationDriver(QObject* parent)
    : QAnimationDriver(parent)
{
    // Precise: a coarse timer may fire up to 5% late, which at 6 ms frames
    // means regularly missing a refresh.
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QChronoTimer::timeout, this, &DisplayAnimationDriver::advance);
}

void DisplayAnimationDriver::start()
{
    // Read on every start, so a monitor plugged in or switched to another
    // mode is picked up by the next animation.
    timer_.setInterval(displayFrameInterval());
    timer_.start();
    QAnimationDriver::start();
}

void DisplayAnimationDriver::stop()
{
    timer_.stop();
    QAnimationDriver::stop();
}

} // namespace Ui
