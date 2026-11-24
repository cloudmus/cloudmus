#include "Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace Config {

namespace {

constexpr char kSubdirectory[] = "/cloudmus/fronts/qt";

QString legacyDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String(kSubdirectory);
}

// Moves source to target: as it is when the target is free, entry by entry
// when both are directories (an old version run after the migration makes a
// new crashes/ next to the moved one), and not at all when a file is in the
// way.
void moveInto(const QString& source, const QString& target)
{
    if (!QFileInfo::exists(target)) {
        // Fails across filesystems: then it stays where it is.
        QDir().rename(source, target);
        return;
    }
    if (!QFileInfo(source).isDir() || !QFileInfo(target).isDir())
        return;
    const QDir sourceDir(source);
    const QDir targetDir(target);
    const QStringList names
        = sourceDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString& name : names)
        moveInto(sourceDir.filePath(name), targetDir.filePath(name));
    // Only when nothing is left in it.
    QDir().rmdir(source);
}

} // namespace

QString stateHomeFor(const QString& xdgStateHome, const QString& homeDir)
{
    if (QDir::isAbsolutePath(xdgStateHome))
        return QDir::cleanPath(xdgStateHome);
    return QDir::cleanPath(homeDir + QStringLiteral("/.local/state"));
}

QString stateDir()
{
#ifdef Q_OS_WIN
    const QString home = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
#else
    // Not QStandardPaths::GenericStateLocation: it needs Qt 6.7.
    const QString home = QStandardPaths::isTestModeEnabled()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/state")
        : stateHomeFor(qEnvironmentVariable("XDG_STATE_HOME"), QDir::homePath());
#endif
    return home + QLatin1String(kSubdirectory);
}

void migrateLegacyState() { migrateLegacyState(legacyDir(), stateDir()); }

void migrateLegacyState(const QString& configDir, const QString& stateDir)
{
    if (QDir::cleanPath(configDir) == QDir::cleanPath(stateDir))
        return;
    const QDir from(configDir);
    if (!from.exists())
        return;
    QStringList names { QStringLiteral("debug.log"), QStringLiteral("crashes") };
    names += from.entryList({ QStringLiteral("asan.log.*") }, QDir::Files);
    QDir().mkpath(stateDir);
    const QDir to(stateDir);
    for (const QString& name : names) {
        if (QFileInfo::exists(from.filePath(name)))
            moveInto(from.filePath(name), to.filePath(name));
    }
}

} // namespace Config
