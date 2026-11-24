#pragma once

#include <QString>
#include <QtGlobal>

namespace Playback::DiskSpace {

// How much of the disk is left alone, whatever else there is to save. On Android
// this is more than a nuisance: a head unit whose disk fills up may not boot
// again, so there's a wide margin, and writes stop well before the system's own
// low-storage line (500 MB at most).
#ifdef Q_OS_ANDROID
inline constexpr qint64 kLowBytes = qint64(2048) * 1024 * 1024;
inline constexpr qint64 kCriticalBytes = qint64(1024) * 1024 * 1024;
#else
inline constexpr qint64 kLowBytes = qint64(1024) * 1024 * 1024;
inline constexpr qint64 kCriticalBytes = qint64(200) * 1024 * 1024;
#endif

// What a program without special rights may still write where `path` is (the
// nearest existing folder up from it); -1 if that can't be told.
qint64 freeBytes(const QString& path);

// Not knowing counts as having none on Android, where being wrong is what can't
// be undone. On a desktop it's rather an odd network drive, and no reason to stop.
inline bool freeIsEnough(qint64 free, qint64 needed)
{
    if (free < 0) {
#ifdef Q_OS_ANDROID
        return false;
#else
        return true;
#endif
    }
    return free >= needed;
}

// Whether `bytes` more may be written there and still leave kLowBytes.
bool hasRoom(const QString& path, qint64 bytes = 0);

} // namespace Playback::DiskSpace
