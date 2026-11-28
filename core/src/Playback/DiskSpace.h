#pragma once

#include <QString>
#include <QtGlobal>

namespace Playback::DiskSpace {

// How much of the disk is left alone, whatever else there is to save: the
// user's setting (Config::Settings::diskReserveMb()), the same on every
// platform. Below it caches give way first and nothing new is downloaded; on
// a head unit a full disk may even keep the system from booting again.
inline constexpr int kDefaultReserveMb = 1024;
qint64 reserveBytes();
void setReserveBytes(qint64 bytes);

// What a program without special rights may still write where `path` is (the
// nearest existing folder up from it); -1 if that can't be told.
qint64 freeBytes(const QString& path);

// Not knowing is no reason to stop: an odd network drive, a system that won't
// say. Anything that writes still fails by itself when the disk is full.
inline bool freeIsEnough(qint64 free, qint64 needed) { return free < 0 || free >= needed; }

// Whether `bytes` more may be written there and still leave the reserve.
bool hasRoom(const QString& path, qint64 bytes = 0);

} // namespace Playback::DiskSpace
