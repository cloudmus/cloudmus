#include "DownloadPaths.h"

#include <QDir>
#include <QObject>

namespace Library {

namespace {
// Well under the usual 255-byte file name limit even in multi-byte UTF-8.
constexpr int kMaxSegmentLength = 100;
} // namespace

QString safePathSegment(const QString& name, const QString& fallback)
{
    QString segment;
    segment.reserve(name.size());
    for (const QChar c : name) {
        const bool reserved = c == u'/' || c == u'\\' || c == u':' || c == u'*' || c == u'?' || c == u'"' || c == u'<'
            || c == u'>' || c == u'|' || c.category() == QChar::Other_Control;
        segment += reserved ? QChar(u'_') : c;
    }
    segment = segment.simplified();
    while (segment.startsWith(u'.'))
        segment.remove(0, 1);
    segment = segment.left(kMaxSegmentLength).trimmed();
    // Trailing dots and spaces are silently dropped by FAT, turning
    // "Vol. 2." into another folder than the one created here.
    while (segment.endsWith(u'.') || segment.endsWith(u' '))
        segment.chop(1);
    return segment.isEmpty() ? fallback : segment;
}

QString downloadDirectoryFor(
    const QString& root, Config::Settings::DownloadLayout layout, const QString& sourceName, const Track& track)
{
    using Layout = Config::Settings::DownloadLayout;
    const QString artist = safePathSegment(
        track.artists.isEmpty() ? QString() : track.artists.constFirst().name, QObject::tr("Unknown Artist"));

    QStringList segments;
    switch (layout) {
        case Layout::Flat:
            break;
        case Layout::BySource:
            segments << safePathSegment(sourceName, QObject::tr("Unknown Source"));
            break;
        case Layout::ByArtist:
            segments << artist;
            break;
        case Layout::ByArtistAlbum:
            segments << artist
                     << safePathSegment(track.album ? track.album->title : QString(), QObject::tr("Unknown Album"));
            break;
    }

    QString path = QDir::cleanPath(root);
    for (const QString& segment : segments)
        path += u'/' + segment;
    return path;
}

} // namespace Library
