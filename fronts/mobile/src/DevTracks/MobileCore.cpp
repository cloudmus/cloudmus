#include "MobileCore.h"

#include "Log.h"

namespace App {

MobileCore::MobileCore(const Paths& paths, QObject* parent)
    : QObject(parent)
    , settingsStore_(paths.settingsFile, QSettings::IniFormat)
    , player_(Playback::loadTracks(paths.media), settingsStore_)
    , nowPlaying_(player_)
    , activePlaylist_(player_)
    , settings_(settingsStore_)
{
    connect(&player_, &Playback::QueuePlayer::firstAudio, this, [] { Logging::mark(QStringLiteral("first audio")); });
}

} // namespace App
