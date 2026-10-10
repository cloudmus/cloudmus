#pragma once

#include <QImage>
#include <QString>

#include "Models.h"

namespace Library {

// The file extension (with the dot) for the audio in `path`, told from its
// first bytes — a track in the playback cache has no name to go by; empty
// when it is none of the formats saved here (.mp3, .m4a, .webm, .opus,
// .ogg, .flac).
QString audioExtension(const QString& path);

// Saves a track the front has downloaded as `destDir/<Artists> - <Title>`
// plus the extension its content calls for: a copy of `cachedFile` (which
// stays where it is), tagged with the track's title, artists and album and
// with `cover` (null: none) as its picture. A file of the same name is
// replaced. Tagging is best effort — a format TagLib can't write, or a
// failure, leaves the copy untagged.
//
// Returns the path saved to, or an empty string when nothing was (a format
// not recognised, `destDir` unwritable) — nothing is left behind then.
QString saveTrackFile(const QString& cachedFile, const QString& destDir, const Track& track, const QImage& cover);

} // namespace Library
