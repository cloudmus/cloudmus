#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <fileref.h>
#include <tpropertymap.h>
#include <tvariant.h>

#include "TrackFile.h"

namespace Tests {

namespace {

// Bytes that are the start of each format, for telling them apart.
QByteArray mp3()
{
    // An ID3v2 header with no frames, then MPEG1 Layer III frames (128 kbps,
    // 44.1 kHz): enough of a file for TagLib to find and tag.
    QByteArray data("ID3\x04\x00\x00\x00\x00\x00\x00", 10);
    QByteArray frame("\xff\xfb\x90\x00", 4);
    frame += QByteArray(417 - 4, '\0');
    for (int i = 0; i < 8; ++i)
        data += frame;
    return data;
}

QByteArray flac()
{
    // The marker and a lone, last STREAMINFO block.
    QByteArray data("fLaC", 4);
    data += QByteArray("\x80\x00\x00\x22", 4);
    QByteArray info(34, '\0');
    info[0] = 0x10; // min/max block size 4096
    info[2] = 0x10;
    info[10] = char(0x0a); // 44100 Hz, 2 channels, 16 bits
    info[11] = char(0xc4);
    info[12] = 0x42;
    info[13] = char(0xf0);
    return data + info;
}

QByteArray write(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() ? data : QByteArray();
}

Track track()
{
    Track t;
    t.id = QStringLiteral("1");
    t.title = QStringLiteral("My Song");
    t.artists = { Artist { QStringLiteral("a1"), QStringLiteral("First") },
        Artist { QStringLiteral("a2"), QStringLiteral("Second") } };
    t.album = Album { QStringLiteral("al"), QStringLiteral("My Album"), std::nullopt };
    t.durationMs = 1000;
    return t;
}

QImage cover(int edge = 800)
{
    QImage image(edge, edge, QImage::Format_RGB32);
    image.fill(QColor(200, 60, 30));
    return image;
}

} // namespace

class TrackFileTest : public QObject {
    Q_OBJECT

private slots:
    void theFormatIsToldFromTheContent()
    {
        QTemporaryDir dir;
        const auto extensionOf = [&](const QByteArray& head) {
            const QString path = dir.filePath(QStringLiteral("cached")); // no extension, as in the cache
            QFile::remove(path);
            write(path, head + QByteArray(32, '\0'));
            return Library::audioExtension(path);
        };
        QCOMPARE(extensionOf(QByteArray("ID3\x04", 4)), QStringLiteral(".mp3"));
        QCOMPARE(extensionOf(QByteArray("\xff\xfb\x90\x00", 4)), QStringLiteral(".mp3"));
        QCOMPARE(extensionOf(QByteArray("\x00\x00\x00\x18"
                                        "ftypM4A ",
                     12)),
            QStringLiteral(".m4a"));
        QCOMPARE(extensionOf(QByteArray("\x1a\x45\xdf\xa3", 4)), QStringLiteral(".webm"));
        QCOMPARE(
            extensionOf(QByteArray("OggS\x00\x02", 6) + QByteArray(20, '\0') + "OpusHead"), QStringLiteral(".opus"));
        QCOMPARE(
            extensionOf(QByteArray("OggS\x00\x02", 6) + QByteArray(20, '\0') + "\x01vorbis"), QStringLiteral(".ogg"));
        QCOMPARE(extensionOf("fLaC"), QStringLiteral(".flac"));
        QCOMPARE(extensionOf("<html>not audio</html>"), QString());
        QCOMPARE(Library::audioExtension(dir.filePath(QStringLiteral("missing"))), QString());
    }

    void anMp3IsCopiedNamedAndTagged()
    {
        QTemporaryDir cacheDir, destDir;
        const QString cached = cacheDir.filePath(QStringLiteral("3f2a"));
        const QByteArray audio = write(cached, mp3());
        QVERIFY(!audio.isEmpty());

        const QString saved = Library::saveTrackFile(cached, destDir.path(), track(), cover());
        QCOMPARE(saved, destDir.filePath(QStringLiteral("First, Second - My Song.mp3")));
        QVERIFY(QFile::exists(cached)); // the cache keeps its file

        TagLib::FileRef ref(QFile::encodeName(saved).constData());
        QVERIFY(!ref.isNull());
        QCOMPARE(QString::fromUtf8(ref.tag()->title().toCString(true)), QStringLiteral("My Song"));
        QCOMPARE(QString::fromUtf8(ref.tag()->artist().toCString(true)), QStringLiteral("First, Second"));
        QCOMPARE(QString::fromUtf8(ref.tag()->album().toCString(true)), QStringLiteral("My Album"));
        const auto pictures = ref.complexProperties("PICTURE");
        QCOMPARE(pictures.size(), 1u);
        const TagLib::ByteVector data = pictures.front().value("data").toByteVector();
        QVERIFY(data.size() > 100);
        // A JPEG, scaled down.
        QImage decoded;
        QVERIFY(decoded.loadFromData(reinterpret_cast<const uchar*>(data.data()), int(data.size())));
        QCOMPARE(decoded.width(), 600);

        // The audio itself is untouched, behind the tag.
        QFile file(saved);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().endsWith(audio.mid(10)));
    }

    void aFlacIsTaggedToo()
    {
        QTemporaryDir cacheDir, destDir;
        const QString cached = cacheDir.filePath(QStringLiteral("flac"));
        QVERIFY(!write(cached, flac()).isEmpty());
        const QString saved = Library::saveTrackFile(cached, destDir.path(), track(), cover(300));
        QCOMPARE(QFileInfo(saved).fileName(), QStringLiteral("First, Second - My Song.flac"));
        TagLib::FileRef ref(QFile::encodeName(saved).constData());
        QVERIFY(!ref.isNull());
        QCOMPARE(QString::fromUtf8(ref.tag()->title().toCString(true)), QStringLiteral("My Song"));
        QCOMPARE(ref.complexProperties("PICTURE").size(), 1u);
    }

    // The real thing: what an encoder writes, as the services serve it.
    void realFilesAreTaggedWithTheirCover_data()
    {
        QTest::addColumn<QString>("sample");
        QTest::addColumn<QString>("extension");
        QTest::newRow("m4a (YouTube)") << QStringLiteral("m4a.sample") << QStringLiteral(".m4a");
        QTest::newRow("webm (YouTube)") << QStringLiteral("webm.sample") << QStringLiteral(".webm");
        QTest::newRow("opus") << QStringLiteral("opus.sample") << QStringLiteral(".opus");
        QTest::newRow("mp3 (Yandex)") << QStringLiteral("mp3.sample") << QStringLiteral(".mp3");
    }

    void realFilesAreTaggedWithTheirCover()
    {
        QFETCH(QString, sample);
        QFETCH(QString, extension);
        QTemporaryDir cacheDir, destDir;
        // No extension, as in the cache.
        const QString cached = cacheDir.filePath(QStringLiteral("cached"));
        QVERIFY(QFile::copy(QFINDTESTDATA(QStringLiteral("data/") + sample), cached));

        const QString saved = Library::saveTrackFile(cached, destDir.path(), track(), cover());
        QCOMPARE(QFileInfo(saved).fileName(), QStringLiteral("First, Second - My Song") + extension);
        TagLib::FileRef ref(QFile::encodeName(saved).constData());
        QVERIFY(!ref.isNull());
        QVERIFY(ref.audioProperties() != nullptr); // still a file TagLib can read
        QCOMPARE(QString::fromUtf8(ref.tag()->title().toCString(true)), QStringLiteral("My Song"));
        QCOMPARE(QString::fromUtf8(ref.tag()->artist().toCString(true)), QStringLiteral("First, Second"));
        // WebM (Matroska) leaves the album out, see writeTags().
        QCOMPARE(QString::fromUtf8(ref.tag()->album().toCString(true)),
            extension == QStringLiteral(".webm") ? QString() : QStringLiteral("My Album"));
        QCOMPARE(ref.complexProperties("PICTURE").size(), 1u);
    }

    void aFileThatIsNotAudioIsNotSaved()
    {
        QTemporaryDir cacheDir, destDir;
        const QString cached = cacheDir.filePath(QStringLiteral("junk"));
        QVERIFY(!write(cached, "<html>not audio at all</html>").isEmpty());
        QCOMPARE(Library::saveTrackFile(cached, destDir.path(), track(), cover()), QString());
        QCOMPARE(QDir(destDir.path()).entryList(QDir::Files).size(), 0);
        QCOMPARE(Library::saveTrackFile(cacheDir.filePath(QStringLiteral("gone")), destDir.path(), track(), cover()),
            QString());
    }

    void unsafeCharactersAreReplacedAndAnOldFileIsOverwritten()
    {
        QTemporaryDir cacheDir, destDir;
        const QString cached = cacheDir.filePath(QStringLiteral("a"));
        QVERIFY(!write(cached, mp3()).isEmpty());
        Track t = track();
        t.title = QStringLiteral("Song: Part 2/2");
        t.artists = { Artist { QStringLiteral("a"), QStringLiteral("A/C") } };

        const QString first = Library::saveTrackFile(cached, destDir.path(), t, QImage());
        QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("A_C - Song_ Part 2_2.mp3"));
        QVERIFY(!write(first, "old").isEmpty());
        const QString again = Library::saveTrackFile(cached, destDir.path(), t, QImage());
        QCOMPARE(again, first);
        QVERIFY(QFileInfo(again).size() > 3000);
        QCOMPARE(QDir(destDir.path()).entryList(QDir::Files).size(), 1);

        // No cover: tags only.
        TagLib::FileRef ref(QFile::encodeName(again).constData());
        QCOMPARE(ref.complexProperties("PICTURE").size(), 0u);
        QCOMPARE(QString::fromUtf8(ref.tag()->title().toCString(true)), QStringLiteral("Song: Part 2/2"));
    }
};

QObject* makeTrackFileTest() { return new TrackFileTest; }

} // namespace Tests

#include "TrackFileTest.moc"
