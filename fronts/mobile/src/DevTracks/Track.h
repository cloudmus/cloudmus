#pragma once

#include <QList>
#include <QString>

namespace Playback {

// A track the mobile MVP plays: one of the dev files listed in tracks.json
// (tools/fetch-dev-tracks.sh). The real front will take the protocol's
// Track from the backends instead.
struct Track {
    QString id;
    QString file; // absolute path
    QString title;
    QString artist;
    qint64 durationMs = 0;
    QString cover; // absolute path, empty without one
};

// Reads `dir`/tracks.json; empty (with a warning) when it's missing or bad.
QList<Track> loadTracks(const QString& dir);

} // namespace Playback
