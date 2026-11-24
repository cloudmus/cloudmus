#include <QSignalSpy>
#include <QTest>

#include "TrackStates.h"

namespace Tests {

class TrackStatesTest : public QObject {
    Q_OBJECT

private slots:
    // Asked for only while it isn't known: whoever finds it out is not
    // asked again.
    void streamQualityIsAskedForWhileUnknown()
    {
        Library::TrackStates states;
        QSignalSpy wanted(&states, &Library::TrackStates::streamQualityWanted);
        states.requestStreamQuality(QStringLiteral("s"), QStringLiteral("1"));
        QCOMPARE(wanted.size(), 1);
        QCOMPARE(wanted.first().at(1).toString(), QStringLiteral("1"));

        states.setStreamQuality(QStringLiteral("s"), QStringLiteral("1"), { QStringLiteral("opus"), 129 });
        states.requestStreamQuality(QStringLiteral("s"), QStringLiteral("1"));
        QCOMPARE(wanted.size(), 1);
    }

    void streamQualityIsKeptPerTrackAndAnnounced()
    {
        Library::TrackStates states;
        QSignalSpy changed(&states, &Library::TrackStates::changed);

        states.setStreamQuality(QStringLiteral("s"), QStringLiteral("1"), { QStringLiteral("opus"), 129 });

        const auto quality = states.state(QStringLiteral("s"), QStringLiteral("1")).streamQuality;
        QVERIFY(quality.has_value());
        QCOMPARE(quality->codec, QStringLiteral("opus"));
        QCOMPARE(quality->bitrateKbps, 129);
        QCOMPARE(changed.count(), 1);
        QVERIFY(!states.state(QStringLiteral("s"), QStringLiteral("2")).streamQuality.has_value());
    }

    void aStreamWithoutCodecAndBitrateIsNotRecorded()
    {
        Library::TrackStates states;
        QSignalSpy changed(&states, &Library::TrackStates::changed);

        states.setStreamQuality(QStringLiteral("s"), QStringLiteral("1"), { });

        QVERIFY(!states.state(QStringLiteral("s"), QStringLiteral("1")).streamQuality.has_value());
        QCOMPARE(changed.count(), 0);
    }

    void theSameQualityAgainTellsNothing()
    {
        Library::TrackStates states;
        states.setStreamQuality(QStringLiteral("s"), QStringLiteral("1"), { QStringLiteral("mp3"), 320 });
        QSignalSpy changed(&states, &Library::TrackStates::changed);

        states.setStreamQuality(QStringLiteral("s"), QStringLiteral("1"), { QStringLiteral("mp3"), 320 });
        QCOMPARE(changed.count(), 0);

        states.setStreamQuality(QStringLiteral("s"), QStringLiteral("1"), { QStringLiteral("mp3"), 192 });
        QCOMPARE(changed.count(), 1);
    }

    void aTrackCarriesItsQualityAndTheStreamsOwnTakesOver()
    {
        Library::TrackStates states;
        Track track;
        track.id = QStringLiteral("1");
        track.codec = QStringLiteral("flac");
        track.bitrateKbps = 905;

        states.observe(QStringLiteral("s"), track);
        auto quality = states.state(QStringLiteral("s"), QStringLiteral("1")).streamQuality;
        QVERIFY(quality.has_value());
        QCOMPARE(quality->codec, QStringLiteral("flac"));

        states.setStreamQuality(QStringLiteral("s"), QStringLiteral("1"), { QStringLiteral("opus"), 128 });
        track.bitrateKbps = 1000;
        states.observe(QStringLiteral("s"), track);
        quality = states.state(QStringLiteral("s"), QStringLiteral("1")).streamQuality;
        QCOMPARE(quality->codec, QStringLiteral("opus"));
        QCOMPARE(quality->bitrateKbps, 128);
    }
};

QObject* makeTrackStatesTest() { return new TrackStatesTest; }

} // namespace Tests

#include "TrackStatesTest.moc"
