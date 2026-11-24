#pragma once

#include <QByteArrayView>
#include <QHash>
#include <QVector>

#include <deque>

namespace Playback {

// One analysis frame of what mpv plays: its timestamp in the track and
// how loud it is, over the whole band and in the bass alone.
struct LevelReading {
    double pts = 0; // seconds into the track, the same clock as mpv's audio-pts
    double fullDb = 0; // RMS, dB; -inf for silence
    double bassDb = 0;
};

// Collects the readings AudioPlayer's analyzer filter prints (ametadata,
// through mpv's log — the one way to get every frame, each with its own
// timestamp; af-metadata only holds the latest) and hands them out once
// the speakers get to them. They are printed as the filter runs, which is
// ~0.4 s ahead of playback.
class LevelFeed {
public:
    // One line of mpv's log from ffmpeg, e.g.
    //   "Parsed_ametadata_7: frame:12   pts:12288   pts_time:0.278639"
    //   "Parsed_ametadata_7: lavfi.astats.1.RMS_level=-21.5"
    // Anything else is ignored.
    void addLogLine(QByteArrayView line);

    // The readings due by `audioPts` (what plays now), oldest first.
    QVector<LevelReading> take(double audioPts);

    // After a seek or a stop: nothing queued belongs to what plays next.
    void clear();

private:
    struct Queued {
        LevelReading reading;
        int segment; // a new one each time the track's timestamps start over
    };
    struct Partial {
        LevelReading reading;
        bool hasFull = false;
        bool hasBass = false;
    };

    void push(const LevelReading& reading);

    // The timestamp each printing filter last announced, by its name:
    // each prints a frame's header, then that frame's value.
    QHash<QByteArray, double> currentPts_;
    QHash<double, Partial> partial_;
    std::deque<Queued> queue_;
    int segment_ = 0;
    double lastPushedPts_ = -1;
    double lastAudioPts_ = -1;
};

} // namespace Playback
