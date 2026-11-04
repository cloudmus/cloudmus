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
inline const QString mainScreen = QStringLiteral("MainScreen");
inline const QString downloadsScreen = QStringLiteral("DownloadsScreen");
inline const QString downloads = QStringLiteral("Downloads");
inline const QString sources = QStringLiteral("Sources");
inline const QString favorites = QStringLiteral("Favorites");
inline const QString otherPlaylists = QStringLiteral("OtherPlaylists");
inline const QString playlistScreen = QStringLiteral("PlaylistScreen");
inline const QString openTracks = QStringLiteral("OpenTracks");
inline const QString trackScreen = QStringLiteral("TrackScreen");
inline const QString trackPlaylists = QStringLiteral("TrackPlaylists");

} // namespace Remote::Names
