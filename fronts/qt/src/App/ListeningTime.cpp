#include "ListeningTime.h"

#include "Analytics.h"
#include "PlaybackController.h"

namespace App {

namespace {
constexpr int kReportIntervalMs = 60 * 60 * 1000;
constexpr qint64 kMinuteMs = 60 * 1000;
} // namespace

ListeningTime::ListeningTime(Playback::PlaybackController& playback, Analytics& analytics, QObject* parent)
    : QObject(parent)
    , analytics_(analytics)
{
    connect(&playback, &Playback::PlaybackController::playingChanged, this, [this](bool playing) {
        if (playing) {
            if (!playing_.isValid())
                playing_.start();
        } else {
            collect();
            playing_.invalidate();
        }
    });
    hourly_.setInterval(kReportIntervalMs);
    connect(&hourly_, &QTimer::timeout, this, [this]() {
        collect();
        send();
    });
    hourly_.start();
}

void ListeningTime::finish()
{
    collect();
    send();
}

void ListeningTime::collect()
{
    if (playing_.isValid())
        collectedMs_ += playing_.restart();
}

void ListeningTime::send()
{
    // The part of a minute left over waits for the next report.
    const qint64 minutes = collectedMs_ / kMinuteMs;
    if (minutes == 0)
        return;
    analytics_.recordListeningTime(int(minutes));
    collectedMs_ -= minutes * kMinuteMs;
}

} // namespace App
