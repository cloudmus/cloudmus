#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "AudioFileProbe.h"
#include "FakeUpstream.h"

namespace Tests {

class AudioFileProbeTest : public QObject {
    Q_OBJECT

private slots:
    // The file says what it is; nothing has to play for that, and what
    // can't be read is told as nothing known rather than not at all.
    void filesAreReadOneAfterAnother()
    {
        QTemporaryDir dir;
        const QString wave = dir.filePath(QStringLiteral("wave"));
        QFile file(wave);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(FakeUpstream::silence(30)); // 8 kHz, 16 bit, mono
        file.close();
        const QString junk = dir.filePath(QStringLiteral("junk"));
        QFile other(junk);
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.write(QByteArray(4096, 'x'));
        other.close();

        Playback::AudioFileProbe probe;
        QList<Playback::AudioFileProbe::Info> told;
        const auto note = [&told](const Playback::AudioFileProbe::Info& info) { told.append(info); };
        probe.probe(wave, note);
        probe.probe(junk, note);
        probe.probe(dir.filePath(QStringLiteral("missing")), note);
        probe.probe(wave, note);
        QVERIFY(told.isEmpty()); // not while the caller waits
        QTRY_COMPARE(told.size(), 4);

        QCOMPARE(told[0].codec, QStringLiteral("pcm_s16le"));
        QCOMPARE(told[0].bitrateKbps, 128);
        QVERIFY(told[1].codec.isEmpty());
        QCOMPARE(told[1].bitrateKbps, 0);
        QVERIFY(told[2].codec.isEmpty());
        QCOMPARE(told[3].codec, QStringLiteral("pcm_s16le"));
    }

    // Its mpv isn't kept around for nothing, and comes back when needed.
    void theMpvGoesWhenThereIsNothingToRead()
    {
        QTemporaryDir dir;
        const QString wave = dir.filePath(QStringLiteral("wave"));
        QFile file(wave);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(FakeUpstream::silence(30));
        file.close();

        Playback::AudioFileProbe probe;
        probe.setIdleTimeout(50);
        QVERIFY(!probe.hasMpv());
        int told = 0;
        probe.probe(wave, [&told](const Playback::AudioFileProbe::Info&) { ++told; });
        QTRY_COMPARE(told, 1);
        QVERIFY(probe.hasMpv());
        QTRY_VERIFY(!probe.hasMpv());

        probe.probe(wave, [&told](const Playback::AudioFileProbe::Info& info) {
            if (info.bitrateKbps == 128)
                ++told;
        });
        QTRY_COMPARE(told, 2);
    }
};

QObject* makeAudioFileProbeTest() { return new AudioFileProbeTest; }

} // namespace Tests

#include "AudioFileProbeTest.moc"
