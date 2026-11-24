#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace Playback {
class PlaybackController;
}

namespace App {

class Analytics;

// Adds up the time music actually plays and reports it in whole minutes:
// every hour, and what's left at quit (finish()) — how much the player is
// listened to, which counting track starts can't tell.
class ListeningTime : public QObject {
    Q_OBJECT

public:
    ListeningTime(Playback::PlaybackController& playback, Analytics& analytics, QObject* parent = nullptr);

    void finish();

private:
    void collect();
    void send();

    Analytics& analytics_;
    QElapsedTimer playing_; // valid while playing
    qint64 collectedMs_ = 0;
    QTimer hourly_;
};

} // namespace App
