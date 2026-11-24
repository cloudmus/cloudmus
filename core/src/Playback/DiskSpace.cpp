#include "DiskSpace.h"

#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_UNIX
#include <sys/statvfs.h>
#else
#include <QStorageInfo>
#endif

namespace Playback::DiskSpace {

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
    return freeIsEnough(free, kLowBytes) && (free < 0 || free - bytes >= kLowBytes);
}

} // namespace Playback::DiskSpace
