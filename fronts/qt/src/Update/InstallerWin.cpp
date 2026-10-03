#include "Installer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>

namespace Update::Installer {

namespace {
QString tr(const char* text) { return QCoreApplication::translate("Update::Installer", text); }
} // namespace

QString unavailableReason()
{
    // The setup replaces the whole folder it installs to: never point it
    // at one it didn't make (a build directory, an unpacked copy).
    if (!QFile::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/Uninstall.exe")))
        return tr("This copy of CloudMus can't update itself: only an installed one can.");
    return QString();
}

QString downloadPath(const Release& release)
{
    return QDir::tempPath() + u'/' + release.assetName + QStringLiteral(".part");
}

bool launch(const QString& downloadedPath, const Release& release, QString* error)
{
    // Windows runs it by its .exe name only.
    const QString setup = QDir::tempPath() + u'/' + release.assetName;
    QFile::remove(setup);
    if (!QFile::rename(downloadedPath, setup)) {
        *error = tr("Can't rename %1").arg(QDir::toNativeSeparators(downloadedPath));
        QFile::remove(downloadedPath);
        return false;
    }
    QProcess process;
    process.setProgram(setup);
    // Native, not setArguments(): NSIS wants /D= last and unquoted, spaces
    // and all, which QProcess's own quoting would break. /UPDATE: shows
    // only its progress, asks nothing, waits for this process to exit and
    // starts the new version when done (see cloudmus.nsi).
    process.setNativeArguments(
        QStringLiteral("/UPDATE /D=") + QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
    if (!process.startDetached()) {
        *error = process.errorString();
        return false;
    }
    qInfo() << "Update: started" << setup;
    return true;
}

} // namespace Update::Installer
