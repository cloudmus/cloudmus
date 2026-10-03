#include "Installer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <unistd.h>

namespace Update::Installer {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("Update::Installer", text); }

// $APPIMAGE: set by the AppImage runtime to the .AppImage file itself.
QString currentAppImage() { return qEnvironmentVariable("APPIMAGE"); }

// The AppImage's AppRun (packaging/appimage/AppRun) runs the app as its
// child and cleans up after it once it exits — the new version must start
// only after that, or the old run's cleanup removes what the new one just
// set up (the backend manifests).
std::optional<qint64> appRunPid()
{
    const pid_t parent = getppid();
    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(parent));
    if (!cmdline.open(QIODevice::ReadOnly))
        return std::nullopt;
    if (!cmdline.readAll().contains("AppRun"))
        return std::nullopt;
    return parent;
}

} // namespace

QString unavailableReason()
{
    const QString appImage = currentAppImage();
    if (appImage.isEmpty())
        return tr("This copy of CloudMus can't update itself: only the AppImage can.");
    const QString folder = QFileInfo(appImage).absolutePath();
    if (!QFileInfo(folder).isWritable())
        return tr("CloudMus can't update itself: the folder %1 is not writable.").arg(QDir::toNativeSeparators(folder));
    return QString();
}

QString downloadPath(const Release& release)
{
    return QFileInfo(currentAppImage()).absolutePath() + QStringLiteral("/.") + release.assetName
        + QStringLiteral(".part");
}

QString appImageTarget(const QString& currentPath, const QString& assetName)
{
    static const QRegularExpression versioned(QStringLiteral(R"(^CloudMus-.+-x86_64\.AppImage$)"));
    const QFileInfo current(currentPath);
    if (!versioned.match(current.fileName()).hasMatch())
        return currentPath;
    return current.absolutePath() + u'/' + assetName;
}

bool replaceAppImage(const QString& downloadedPath, const QString& currentPath, const QString& target, QString* error)
{
    if (!QFile::setPermissions(downloadedPath,
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup
                | QFile::ReadOther | QFile::ExeOther)) {
        *error = tr("Can't make %1 executable").arg(downloadedPath);
        return false;
    }
    // rename(2), not QFile::rename(): that refuses to replace an existing
    // file, and this has to be atomic when the target is the old AppImage.
    if (std::rename(QFile::encodeName(downloadedPath).constData(), QFile::encodeName(target).constData()) != 0) {
        *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    if (QFileInfo(target) != QFileInfo(currentPath) && !QFile::remove(currentPath))
        qWarning() << "Update: couldn't remove the old AppImage" << currentPath;
    return true;
}

bool launch(const QString& downloadedPath, const Release& release, QString* error)
{
    const QString current = currentAppImage();
    const QString target = appImageTarget(current, release.assetName);
    if (!replaceAppImage(downloadedPath, current, target, error))
        return false;

    QStringList waitFor { QString::number(QCoreApplication::applicationPid()) };
    if (const std::optional<qint64> appRun = appRunPid())
        waitFor.append(QString::number(*appRun));
    QProcess process;
    process.setProgram(QStringLiteral("/bin/sh"));
    // $1 the AppImage, the rest the processes to wait for — all of them.
    process.setArguments(QStringList { QStringLiteral("-c"), QStringLiteral(R"(app="$1"; shift
while :; do
    alive=
    for pid; do kill -0 "$pid" 2>/dev/null && alive=1; done
    [ -z "$alive" ] && break
    sleep 0.2
done
exec "$app")"),
                             QStringLiteral("sh"), target }
        + waitFor);
    // The new AppImage's runtime and AppRun set these for themselves.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const char* name : { "APPIMAGE", "APPDIR", "ARGV0", "OWD" })
        environment.remove(QString::fromLatin1(name));
    process.setProcessEnvironment(environment);
    const QString startedFrom = qEnvironmentVariable("OWD");
    process.setWorkingDirectory(startedFrom.isEmpty() ? QDir::homePath() : startedFrom);
    if (!process.startDetached()) {
        *error = process.errorString();
        return false;
    }
    qInfo() << "Update: installed" << target << "- starting it once this process exits";
    return true;
}

} // namespace Update::Installer
