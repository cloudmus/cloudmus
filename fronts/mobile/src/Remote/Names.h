#pragma once

#include <QString>

// What the core's objects are called over Qt Remote Objects — shared by the
// host (Remote::Host) and the replicas (Ui::Backend).
namespace Remote::Names {

inline const QString nowPlaying = QStringLiteral("NowPlaying");
inline const QString activePlaylist = QStringLiteral("ActivePlaylist");
inline const QString tracks = QStringLiteral("Tracks");
inline const QString settings = QStringLiteral("Settings");

} // namespace Remote::Names
