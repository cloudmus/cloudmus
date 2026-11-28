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
inline const QString openFolders = QStringLiteral("OpenFolders");
inline const QString openEntries = QStringLiteral("OpenEntries");
inline const QString playlistScreens = QStringLiteral("PlaylistScreens");
// How many levels of Playlist pages there are (ViewModel::PlaylistScreens).
inline constexpr int playlistLevels = 8;
// A deeper page's: the first's name with its depth.
inline QString atDepth(const QString& name, int depth) { return depth == 0 ? name : name + QString::number(depth); }
inline const QString trackScreen = QStringLiteral("TrackScreen");
inline const QString trackPlaylists = QStringLiteral("TrackPlaylists");
inline const QString settingsScreen = QStringLiteral("SettingsScreen");
inline const QString settingsSources = QStringLiteral("SettingsSources");
inline const QString settingsProxies = QStringLiteral("SettingsProxies");
inline const QString sourceSettings = QStringLiteral("SourceSettings");
inline const QString sourceConnections = QStringLiteral("SourceConnections");
inline const QString sourceFields = QStringLiteral("SourceFields");
inline const QString pulse = QStringLiteral("Pulse");
inline const QString updateScreen = QStringLiteral("UpdateScreen");
inline const QString signInHelper = QStringLiteral("SignInHelper");

} // namespace Remote::Names
