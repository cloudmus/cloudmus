#pragma once

#include <QString>

#include "Models.h"
#include "Settings.h"

namespace Library {

// Where catalog.downloadTrack should save a track: the configured
// download folder plus the subfolders Config::Settings::DownloadLayout
// asks for. Only the folder is the front's to choose — the file name is
// the source's (docs/protocol.md §7.5).
QString downloadDirectoryFor(
    const QString& root, Config::Settings::DownloadLayout layout, const QString& sourceName, const Track& track);

// One path segment from free-form metadata: no path separators or
// characters FAT/exFAT reject (a music folder synced to a player or a
// USB stick), no leading dots (hidden, or "."/".."), bounded length.
QString safePathSegment(const QString& name, const QString& fallback);

} // namespace Library
