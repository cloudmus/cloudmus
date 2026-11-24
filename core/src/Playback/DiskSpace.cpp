#include "DiskSpace.h"

#include <QFile>
#include <QFileInfo>

#include <atomic>

#ifdef Q_OS_UNIX
#include <sys/statvfs.h>
#else
#include <QStorageInfo>
#endif

namespace Playback::DiskSpace {

namespace {

// Read from wherever something is about to be written, threads included.
std::atomic<qint64> reserve { qint64(kDefaultReserveMb) * 1024 * 1024 };

} // namespace

qint64 reserveBytes() { return reserve.load(std::memory_order_relaxed); }

void setReserveBytes(qint64 bytes) { reserve.store(qMax(qint64(0), bytes), std::memory_order_relaxed); }

qint64 freeBytes(const QString& path)
{
    QFileInfo info(path);
    while (!info.exists()) {
        const QString parent = info.absolutePath();
        if (parent == info.absoluteFilePath())
            return -1;
        info.setFile(parent);
    }
#ifdef Q_OS_UNIX
    // Asked often, and directly: QStorageInfo reads the whole list of mounts.
    struct statvfs stats;
    if (statvfs(QFile::encodeName(info.absoluteFilePath()).constData(), &stats) != 0)
        return -1;
    return qint64(stats.f_bavail) * qint64(stats.f_frsize);
#else
    const QStorageInfo storage(info.absoluteFilePath());
    return storage.isValid() ? storage.bytesAvailable() : -1;
#endif
}

bool hasRoom(const QString& path, qint64 bytes)
{
    const qint64 free = freeBytes(path);
    return free < 0 || free - bytes >= reserveBytes();
}

} // namespace Playback::DiskSpace
