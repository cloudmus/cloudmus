#include "Installer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

namespace Update::Installer {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("Update::Installer", text); }

// Runs a tool to the end; false with its output in `error` if it failed.
// Copying the app takes seconds: meanwhile the window still repaints, but
// takes no clicks or keys — nothing the user does can get in between.
bool run(const QString& program, const QStringList& arguments, QString* error)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    QEventLoop loop;
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    QTimer::singleShot(5 * 60 * 1000, &loop, &QEventLoop::quit);
    process.start(program, arguments);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished();
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
        || process.error() == QProcess::FailedToStart) {
        const QString output = QString::fromLocal8Bit(process.readAll()).trimmed();
        *error = QStringLiteral("%1: %2").arg(
            QFileInfo(program).fileName(), output.isEmpty() ? process.errorString() : output);
        return false;
    }
    return true;
}

QString bundleIdentifier(const QString& bundle)
{
    return QSettings(bundle + QStringLiteral("/Contents/Info.plist"), QSettings::NativeFormat)
        .value(QStringLiteral("CFBundleIdentifier"))
        .toString();
}

QString currentBundle() { return bundleOf(QCoreApplication::applicationDirPath()); }

} // namespace

QString bundleOf(const QString& executableDir)
{
    const QString contents = QStringLiteral("/Contents/MacOS");
    const QString path = QDir::cleanPath(executableDir);
    if (!path.endsWith(contents))
        return QString();
    const QString bundle = path.chopped(contents.size());
    return bundle.endsWith(QLatin1String(".app")) ? bundle : QString();
}

QString unavailableReason(const QString& bundle)
{
    if (bundle.isEmpty())
        return tr("This copy of CloudMus can't update itself: only CloudMus.app can.");
    // Run straight from the DMG, or — still quarantined, from Downloads —
    // from the read-only copy macOS makes of it (App Translocation).
    if (bundle.startsWith(QLatin1String("/Volumes/")) || bundle.contains(QLatin1String("/AppTranslocation/")))
        return tr("CloudMus can't update itself while it runs from the disk image or the Downloads folder: "
                  "move it to Applications first.");
    const QString folder = QFileInfo(bundle).absolutePath();
    if (!QFileInfo(folder).isWritable())
        return tr("CloudMus can't update itself: the folder %1 is not writable.").arg(folder);
    return QString();
}

QString unavailableReason() { return unavailableReason(currentBundle()); }

QString downloadPath(const Release& release)
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(folder);
    return folder + u'/' + release.assetName + QStringLiteral(".part");
}

bool launch(const QString& downloadedPath, const Release& release, QString* error)
{
    Q_UNUSED(release);
    const QString bundle = currentBundle();
    const QString folder = QFileInfo(bundle).absolutePath();
    // Next to the old bundle, on its file system: putting it in place is
    // then a rename, done once this process has exited.
    const QString staged = folder + QStringLiteral("/.CloudMus.app.new");
    const QString old = folder + QStringLiteral("/.CloudMus.app.old");

    QTemporaryDir mountPoint(QDir::tempPath() + QStringLiteral("/cloudmus-update-XXXXXX"));
    if (!mountPoint.isValid()) {
        *error = mountPoint.errorString();
        return false;
    }
    // -noverify: no pass over the image's checksums (a second or two);
    // codesign checks every file of the copied app below instead.
    if (!run(QStringLiteral("/usr/bin/hdiutil"),
            { QStringLiteral("attach"), QStringLiteral("-noverify"), QStringLiteral("-nobrowse"),
                QStringLiteral("-readonly"), QStringLiteral("-noautoopen"), QStringLiteral("-mountpoint"),
                mountPoint.path(), downloadedPath },
            error))
        return false;
    QDir(staged).removeRecursively();
    QDir(old).removeRecursively();
    const QString source = mountPoint.path() + QStringLiteral("/CloudMus.app");
    QString copyError;
    const bool copied
        = QFileInfo(source).isDir() && run(QStringLiteral("/usr/bin/ditto"), { source, staged }, &copyError);
    QString ignored;
    if (!run(QStringLiteral("/usr/bin/hdiutil"),
            { QStringLiteral("detach"), QStringLiteral("-force"), mountPoint.path() }, &ignored))
        qWarning() << "Update: couldn't detach" << mountPoint.path() << ignored;
    QFile::remove(downloadedPath);
    if (!copied) {
        *error = copyError.isEmpty() ? tr("The disk image has no CloudMus.app") : copyError;
        QDir(staged).removeRecursively();
        return false;
    }
    // A damaged copy or some other app must never replace this one.
    if (bundleIdentifier(staged) != bundleIdentifier(bundle)
        || !run(QStringLiteral("/usr/bin/codesign"),
            { QStringLiteral("--verify"), QStringLiteral("--deep"), QStringLiteral("--strict"), staged }, error)) {
        if (error->isEmpty())
            *error = tr("The disk image holds another app");
        QDir(staged).removeRecursively();
        return false;
    }

    QProcess process;
    process.setProgram(QStringLiteral("/bin/sh"));
    // $1 the app, $2 the new copy, $3 where the old one goes meanwhile, $4
    // this process. If the new copy can't take the app's place, the old
    // one goes back and starts again.
    process.setArguments({ QStringLiteral("-c"), QStringLiteral(R"(app="$1" new="$2" old="$3" pid="$4"
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
if ! mv "$app" "$old"; then
    rm -rf "$new"
    exec open "$app"
fi
if ! mv "$new" "$app"; then
    mv "$old" "$app"
    rm -rf "$new"
    exec open "$app"
fi
rm -rf "$old"
exec open "$app")"),
        QStringLiteral("sh"), bundle, staged, old, QString::number(QCoreApplication::applicationPid()) });
    process.setWorkingDirectory(QDir::homePath());
    if (!process.startDetached()) {
        *error = process.errorString();
        QDir(staged).removeRecursively();
        return false;
    }
    qInfo() << "Update: staged" << staged << "- it replaces" << bundle << "once this process exits";
    return true;
}

} // namespace Update::Installer
