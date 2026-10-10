#include "TrackFile.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QObject>

#include <string>

#include <fileref.h>
#include <id3v2tag.h>
#include <matroskafile.h>
#include <mpegfile.h>
#include <tbytevector.h>
#include <tlist.h>
#include <tstring.h>
#include <tvariant.h>

#include "DownloadPaths.h"

namespace Library {

namespace {

Q_LOGGING_CATEGORY(lcTrackFile, "cloudmus.library.trackfile")

// Big enough for any player's cover view, small enough not to double a
// track's size.
constexpr int kCoverEdge = 600;
constexpr int kCoverQuality = 90;

TagLib::String toTagLib(const QString& text) { return TagLib::String(text.toUtf8().constData(), TagLib::String::UTF8); }

QByteArray coverJpeg(const QImage& cover)
{
    if (cover.isNull())
        return { };
    const QImage fitted = qMax(cover.width(), cover.height()) > kCoverEdge
        ? cover.scaled(kCoverEdge, kCoverEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : cover;
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    // JPEG has no alpha: what's transparent would turn black.
    const bool saved = fitted.convertedTo(QImage::Format_RGB32).save(&buffer, "JPEG", kCoverQuality);
    return saved ? jpeg : QByteArray();
}

void writeTags(const QString& path, const Track& track, const QImage& cover)
{
    // TagLib opens a wide name on Windows and a native-encoded one elsewhere,
    // and keeps only a pointer to it: the string must outlive the FileRef.
#ifdef Q_OS_WIN
    const std::wstring name = path.toStdWString();
    TagLib::FileRef file(name.c_str());
#else
    const QByteArray name = QFile::encodeName(path);
    TagLib::FileRef file(name.constData());
#endif
    if (file.isNull() || file.tag() == nullptr) {
        qCDebug(lcTrackFile) << "no tags can be written to" << path;
        return;
    }

    QStringList artists;
    for (const Artist& artist : track.artists)
        artists << artist.name;
    TagLib::Tag* tag = file.tag();
    tag->setTitle(toTagLib(track.title));
    tag->setArtist(toTagLib(artists.join(QStringLiteral(", "))));
    // Matroska keeps the album as a TITLE of its own, one level above the
    // track's — and ffmpeg, so mpv and most players, folds the two into one
    // "title": the album's would show as the track's. Better none.
    const bool matroska = dynamic_cast<TagLib::Matroska::File*>(file.file()) != nullptr;
    if (track.album && !matroska)
        tag->setAlbum(toTagLib(track.album->title));

    const QByteArray jpeg = coverJpeg(cover);
    if (!jpeg.isEmpty()) {
        TagLib::VariantMap picture;
        picture.insert("data", TagLib::ByteVector(jpeg.constData(), static_cast<unsigned int>(jpeg.size())));
        picture.insert("mimeType", TagLib::String("image/jpeg"));
        picture.insert("pictureType", TagLib::String("Front Cover"));
        // Not every format can hold one; the rest of the tags go in all the same.
        if (!file.setComplexProperties("PICTURE", TagLib::List<TagLib::VariantMap>().append(picture)))
            qCDebug(lcTrackFile) << "no cover can be written to" << path;
    }
    // An MP3 gets ID3v2.3 alone: the default adds an ID3v1 tag at the end too,
    // which cuts every field to 30 characters, and v2.3 is what the likes of
    // Windows Explorer read in full.
    bool saved;
    if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(file.file()))
        saved = mpeg->save(TagLib::MPEG::File::ID3v2, TagLib::File::StripOthers, TagLib::ID3v2::v3);
    else
        saved = file.save();
    if (!saved)
        qCWarning(lcTrackFile) << "couldn't write the tags of" << path;
}

} // namespace

QString audioExtension(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { };
    const QByteArray head = file.read(64);
    if (head.size() >= 12 && head.mid(4, 4) == "ftyp")
        return QStringLiteral(".m4a");
    if (head.startsWith("\x1a\x45\xdf\xa3"))
        return QStringLiteral(".webm");
    if (head.startsWith("OggS"))
        return head.contains("OpusHead") ? QStringLiteral(".opus") : QStringLiteral(".ogg");
    if (head.startsWith("fLaC"))
        return QStringLiteral(".flac");
    // An ID3 tag, or the sync of an MPEG audio frame (11 set bits).
    if (head.startsWith("ID3")
        || (head.size() >= 2 && static_cast<uchar>(head[0]) == 0xFF && (static_cast<uchar>(head[1]) & 0xE0) == 0xE0))
        return QStringLiteral(".mp3");
    return { };
}

QString saveTrackFile(const QString& cachedFile, const QString& destDir, const Track& track, const QImage& cover)
{
    const QString extension = audioExtension(cachedFile);
    if (extension.isEmpty()) {
        qCDebug(lcTrackFile) << "not a format to save:" << cachedFile;
        return { };
    }
    if (!QDir().mkpath(destDir))
        return { };

    QStringList artists;
    for (const Artist& artist : track.artists)
        artists << artist.name;
    const QString name = safePathSegment(
        QStringLiteral("%1 - %2").arg(
            artists.isEmpty() ? QObject::tr("Unknown Artist") : artists.join(QStringLiteral(", ")), track.title),
        QObject::tr("Unknown track"));
    const QString path = QDir(destDir).filePath(name + extension);

    // Replaced like a backend's download did; the copy goes under its
    // final name so TagLib finds the format by the extension.
    QFile::remove(path);
    if (!QFile::copy(cachedFile, path)) {
        qCWarning(lcTrackFile) << "can't copy" << cachedFile << "to" << path;
        QFile::remove(path);
        return { };
    }
    QFile(path).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    writeTags(path, track, cover);
    return path;
}

} // namespace Library
