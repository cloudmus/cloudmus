#include <QSignalSpy>
#include <QTest>

#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

#include "AudioPulse.h"
#include "BeatDetector.h"
#include "LevelFeed.h"

namespace Tests {

namespace {

using Playback::LevelReading;

constexpr double kFrame = 1024.0 / 44100;
constexpr double kInf = std::numeric_limits<double>::infinity();

// A mastered-sounding mix, frame by frame: the full band loud and nearly
// flat; the bass wobbling under a kick every `kickEvery` seconds from 1 s
// on (none for 0), with a hi-hat between the kicks that only the full
// band hears. The kicks numbered in `inaudible` (from 0) are lost under
// the mix: in `kicks` all the same, but nothing to hear.
QVector<LevelReading> mix(double seconds, double kickEvery, QVector<double>* kicks, const QSet<int>& inaudible = { })
{
    QVector<LevelReading> frames;
    unsigned seed = 12345;
    const auto jitter = [&seed](double amount) {
        seed = seed * 1103515245u + 12345u;
        return amount * (double((seed >> 16) & 0x7fff) / 0x7fff * 2 - 1);
    };
    for (int i = 0; i * kFrame < seconds; ++i) {
        const double t = i * kFrame;
        LevelReading r { t, -10 + jitter(0.5), -28 + jitter(1.5) };
        if (kickEvery > 0 && t >= 1) {
            const double sinceKick = std::fmod(t - 1, kickEvery);
            const int frameOfKick = int(sinceKick / kFrame);
            static const double kKick[] = { -8, -13, -19, -24 };
            const int kick = int((t - 1) / kickEvery + 1e-9);
            if (frameOfKick < 4 && !inaudible.contains(kick))
                r.bassDb = std::max(r.bassDb, kKick[frameOfKick]);
            if (frameOfKick == 0 && kicks)
                kicks->append(t);
            if (std::abs(sinceKick - kickEvery / 2) < kFrame / 2)
                r.fullDb += 4; // hi-hat
        }
        frames.append(r);
    }
    return frames;
}

QVector<double> beatsOf(const QVector<LevelReading>& frames)
{
    Playback::BeatDetector detector;
    QVector<double> beats;
    for (const LevelReading& r : frames) {
        if (detector.feed(r).beat)
            beats.append(r.pts);
    }
    return beats;
}

void addFrame(Playback::LevelFeed& feed, int filter, double pts, int channel, const char* db)
{
    const QByteArray name = "Parsed_ametadata_" + QByteArray::number(filter) + ": ";
    feed.addLogLine(QByteArray(name + "frame:0    pts:0       pts_time:" + QByteArray::number(pts)));
    feed.addLogLine(QByteArray(name + "lavfi.astats." + QByteArray::number(channel) + ".RMS_level=" + db));
}

// Both printing filters' lines for one frame, as mpv logs them.
void addReading(Playback::LevelFeed& feed, double pts, const char* fullDb = "-20", const char* bassDb = "-30")
{
    addFrame(feed, 7, pts, 1, fullDb);
    addFrame(feed, 8, pts, 2, bassDb);
}

} // namespace

class AudioPulseTest : public QObject {
    Q_OBJECT

private slots:
    // --- LevelFeed

    void parsesTheAnalyzersLogLines()
    {
        Playback::LevelFeed feed;
        // Copied from mpv's log.
        feed.addLogLine("Parsed_ametadata_7: frame:0    pts:0       pts_time:0");
        feed.addLogLine("Parsed_ametadata_7: lavfi.astats.1.RMS_level=-34.344904");
        feed.addLogLine("Parsed_ametadata_8: frame:0    pts:0       pts_time:0");
        feed.addLogLine("Parsed_ametadata_8: lavfi.astats.2.RMS_level=-inf");
        feed.addLogLine("Some other ffmpeg line");
        feed.addLogLine("Parsed_ametadata_8: garbage");
        const auto readings = feed.take(1);
        QCOMPARE(readings.size(), 1);
        QCOMPARE(readings[0].pts, 0.0);
        QCOMPARE(readings[0].fullDb, -34.344904);
        QCOMPARE(readings[0].bassDb, -kInf);
    }

    void aFrameNeedsBothBands()
    {
        Playback::LevelFeed feed;
        addFrame(feed, 7, 0.5, 1, "-20");
        QVERIFY(feed.take(10).isEmpty());
        addFrame(feed, 8, 0.5, 2, "-30");
        QCOMPARE(feed.take(10).size(), 1);
    }

    void handsOutReadingsOnlyOnceTheyArePlayed()
    {
        Playback::LevelFeed feed;
        for (int i = 0; i < 10; ++i)
            addReading(feed, i * 0.1);
        QCOMPARE(feed.take(0.25).size(), 3); // 0, 0.1, 0.2
        QVERIFY(feed.take(0.25).isEmpty()); // taken already
        QCOMPARE(feed.take(0.55).size(), 3);
        feed.clear();
        QVERIFY(feed.take(10).isEmpty());
    }

    void theNextTracksReadingsWaitForItsStart()
    {
        // Gapless: the next track's frames (from 0 again) are printed while
        // the end of this one still plays.
        Playback::LevelFeed feed;
        for (int i = 0; i < 5; ++i)
            addReading(feed, 100 + i * 0.1);
        for (int i = 0; i < 5; ++i)
            addReading(feed, i * 0.1);
        QCOMPARE(feed.take(100.25).size(), 3); // not the next track's 0..0.4
        QVERIFY(feed.take(100.25).isEmpty());
        // The next track starts: what's left of this one is past.
        const auto next = feed.take(0.15);
        QCOMPARE(next.size(), 2);
        QCOMPARE(next[0].pts, 0.0);
    }

    // --- BeatDetector

    void normalizesDecibels()
    {
        using Playback::BeatDetector;
        QCOMPARE(BeatDetector::normalize(0), 1.0f);
        QCOMPARE(BeatDetector::normalize(-50), 0.0f);
        QCOMPARE(BeatDetector::normalize(-80), 0.0f);
        QCOMPARE(BeatDetector::normalize(-kInf), 0.0f);
        QCOMPARE(BeatDetector::normalize(-25), 0.5f);
    }

    void findsEachKickOnItsFrame()
    {
        QVector<double> kicks;
        const QVector<double> beats = beatsOf(mix(10, 0.5, &kicks));
        QCOMPARE(beats.size(), kicks.size()); // and none for the hi-hats
        for (int i = 0; i < beats.size(); ++i)
            QVERIFY2(std::abs(beats[i] - kicks[i]) <= kFrame + 1e-9, qPrintable(QString::number(beats[i])));
    }

    void aSteadyMixHasNoBeats() { QVERIFY(beatsOf(mix(10, 0, nullptr)).isEmpty()); }

    void silenceHasNoBeats()
    {
        QVector<LevelReading> frames;
        for (int i = 0; i < 300; ++i)
            frames.append({ i * kFrame, -kInf, -kInf });
        QVERIFY(beatsOf(frames).isEmpty());
    }

    void kicksCloserThanTheRefractoryPeriodAreOne()
    {
        QVector<double> kicks;
        // A kick every 4 frames (~93 ms): too close to be separate beats.
        const QVector<double> beats = beatsOf(mix(4, 4 * kFrame, &kicks));
        QVERIFY(beats.size() < kicks.size());
        for (int i = 1; i < beats.size(); ++i)
            QVERIFY(beats[i] - beats[i - 1] >= 0.12 - 1e-9);
    }

    void fillsInAKickItDidntHear()
    {
        QVector<double> kicks;
        const QVector<double> beats = beatsOf(mix(10, 0.5, &kicks, { 8 }));
        QCOMPARE(beats.size(), kicks.size());
        QVERIFY(std::any_of(
            beats.cbegin(), beats.cend(), [&](double b) { return std::abs(b - kicks[8]) <= kFrame + 1e-9; }));
    }

    void stopsFillingInWhenTheKicksStop()
    {
        QVector<double> kicks;
        const QVector<double> beats = beatsOf(mix(10, 0.5, &kicks, { 8, 9, 10, 11, 12 }));
        const auto near = [&](int kick) {
            return std::any_of(
                beats.cbegin(), beats.cend(), [&](double b) { return std::abs(b - kicks[kick]) <= kFrame + 1e-9; });
        };
        QVERIFY(near(8));
        QVERIFY(near(9));
        QVERIFY(!near(10));
        QVERIFY(!near(11));
        QVERIFY(!near(12));
        QVERIFY(near(13)); // heard again
    }

    void slowKicksGetNoBeatsBetweenThem()
    {
        QVector<double> kicks;
        const QVector<LevelReading> frames = mix(10, 1.0, &kicks);
        QCOMPARE(beatsOf(frames).size(), kicks.size());
    }

    void noTempoNoFillingIn()
    {
        // Kicks at no steady tempo: only what's heard is a beat.
        QVector<LevelReading> frames = mix(8, 0, nullptr);
        const QVector<double> hits { 1.2, 1.72, 2.33, 3.2, 3.58, 4.51, 5.2 }; // after the first second's warmup
        for (const double hit : hits) {
            const int at = int(hit / kFrame);
            static const double kKick[] = { -8, -13, -19, -24 };
            for (int i = 0; i < 4; ++i)
                frames[at + i].bassDb = kKick[i];
        }
        QCOMPARE(beatsOf(frames).size(), hits.size());
    }

    void theLevelRisesFastAndFallsSlowly()
    {
        Playback::BeatDetector detector;
        float level = 0;
        for (int i = 0; i < 8; ++i) // ~190 ms of loud
            level = detector.feed({ i * kFrame, -5, -20 }).level;
        QVERIFY(level > 0.6f);
        level = detector.idle(100);
        QVERIFY(level > 0.3f);
        for (int i = 0; i < 50; ++i)
            level = detector.idle(50);
        QVERIFY(level < 0.01f);
    }

    // --- AudioPulse

    void switchesTheSourceOnlyWhileHeld()
    {
        QVector<bool> switches;
        ViewModel::AudioPulse pulse(
            { [&switches](bool on) { switches.append(on); }, []() { return QVector<LevelReading> { }; } });
        QVERIFY(!pulse.isActive());
        pulse.acquire();
        pulse.acquire();
        QVERIFY(pulse.isActive());
        pulse.release();
        QVERIFY(pulse.isActive()); // another holder is left
        pulse.release();
        QVERIFY(!pulse.isActive());
        QCOMPARE(switches, (QVector<bool> { true, false }));
    }

    void reportsKicksAsTheyArePlayedAndFallsSilentAfter()
    {
        QVector<double> kicks;
        const QVector<LevelReading> frames = mix(4, 0.5, &kicks);
        double now = 0;
        int next = 0;
        ViewModel::AudioPulse pulse({ [](bool) { },
            [&]() {
                QVector<LevelReading> due;
                while (next < frames.size() && frames[next].pts <= now)
                    due.append(frames[next++]);
                return due;
            } });
        QSignalSpy beats(&pulse, &ViewModel::AudioPulse::beat);
        for (; now < 4; now += 0.016)
            pulse.poll(16);
        QCOMPARE(beats.count(), kicks.size());
        QVERIFY(pulse.level() > 0.5f);

        for (int i = 0; i < 100; ++i) // stopped: no more readings
            pulse.poll(16);
        QVERIFY(pulse.level() < 0.01f);
    }

    void keepsItsLevelBetweenFrames()
    {
        // Ticks (16 ms) come more often than frames (23 ms): a tick with
        // none mustn't read as silence.
        double now = 0;
        double nextPts = 0;
        ViewModel::AudioPulse pulse({ [](bool) { },
            [&]() {
                QVector<LevelReading> due;
                while (nextPts <= now) {
                    due.append({ nextPts, -8, -30 });
                    nextPts += kFrame;
                }
                return due;
            } });
        float lowest = 1;
        for (; now < 2; now += 0.016) {
            pulse.poll(16);
            if (now > 0.5)
                lowest = std::min(lowest, pulse.level());
        }
        QVERIFY(lowest > 0.8f);
    }
};

QObject* makeAudioPulseTest() { return new AudioPulseTest; }

} // namespace Tests

#include "AudioPulseTest.moc"
