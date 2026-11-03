#pragma once

#include <QString>

// What the core's objects are called over Qt Remote Objects — shared by the
// host (Remote::Host) and the replicas (Ui::Backend).
namespace Remote::Names {

inline const QString nowPlaying = QStringLiteral("NowPlaying");
inline const QString activePlaylist = QStringLiteral("ActivePlaylist");
inline const QString tracks = QStringLiteral("Tracks");
inline const QString settings = QStringLiteral("Settings");
inline const QString library = QStringLiteral("Library");
inline const QString sources = QStringLiteral("Sources");
inline const QString favorites = QStringLiteral("Favorites");
inline const QString otherPlaylists = QStringLiteral("OtherPlaylists");

} // namespace Remote::Names
