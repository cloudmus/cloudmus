#include "Autostart.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace Integration::Autostart {

namespace {

QString entryPath()
{
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return configHome + QStringLiteral("/autostart/cloudmus-qt.desktop");
}

// Empty unless running from an AppImage.
QString appImagePath() { return QProcessEnvironment::systemEnvironment().value(QStringLiteral("APPIMAGE")); }

// Empty unless a launcher set it: the Nix package's bin/cloudmus, which seeds
// the backend manifests and starts the wrapped binary. The wrapped binary in
// the store is no target for the entry — it skips wrapQtAppsHook's environment,
// and once an update or a garbage collection removed its path nothing starts at
// all.
QString launcherPath() { return QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLOUDMUS_LAUNCHER")); }

QString executablePath()
{
    const QString appImage = appImagePath();
    if (!appImage.isEmpty())
        return appImage;
    const QString launcher = launcherPath();
    return launcher.isEmpty() ? QCoreApplication::applicationFilePath() : launcher;
}

// Exec= quoting per the Desktop Entry spec: a path with reserved
// characters goes in double quotes with ", `, $ and \ backslash-escaped,
// and then — Exec being a string value — every backslash is doubled once
// more.
QString quotedExecArgument(const QString& argument)
{
    static const QRegularExpression reserved(QStringLiteral(R"([\s"'\\><~|&;$*?#()`])"));
    if (!argument.contains(reserved))
        return argument;
    QString escaped;
    for (const QChar c : argument) {
        if (c == u'"' || c == u'`' || c == u'$' || c == u'\\')
            escaped += u'\\';
        escaped += c;
    }
    escaped.replace(u'\\', QStringLiteral("\\\\"));
    return u'"' + escaped + u'"';
}

// The inverse of quotedExecArgument() for the first Exec= argument only —
// all refresh() needs.
QString firstExecArgument(const QString& exec)
{
    if (!exec.startsWith(u'"'))
        return exec.section(u' ', 0, 0);
    QString unescaped = exec;
    unescaped.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    QString result;
    for (int i = 1; i < unescaped.size(); ++i) {
        const QChar c = unescaped[i];
        if (c == u'\\' && i + 1 < unescaped.size())
            result += unescaped[++i];
        else if (c == u'"')
            break;
        else
            result += c;
    }
    return result;
}

// Keys of the [Desktop Entry] group only; enough for the handful read
// here, without QSettings' INI handling (which mangles keys and values it
// considers special).
QHash<QString, QString> readEntry()
{
    QHash<QString, QString> keys;
    QFile file(entryPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return keys;
    bool inMainGroup = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(u'[')) {
            inMainGroup = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (inMainGroup && eq > 0)
            keys.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return keys;
}

bool writeEntry()
{
    const QString path = entryPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "Autostart: can't create" << QFileInfo(path).absolutePath();
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Autostart: can't write" << path << file.errorString();
        return false;
    }
    const QString exec = quotedExecArgument(executablePath()) + u' ' + QLatin1String(kLaunchedAtLoginArgument);
    // Name/Icon match packaging/appimage/cloudmus-qt.desktop, so session
    // settings (Plasma's Autostart page, GNOME Tweaks) list it the same.
    const QString contents = QStringLiteral("[Desktop Entry]\n"
                                            "Type=Application\n"
                                            "Name=CloudMus\n"
                                            "Comment=A multi-source music player\n"
                                            "Exec=%1\n"
                                            "Icon=cloudmus-qt\n"
                                            "Terminal=false\n"
                                            "X-GNOME-Autostart-enabled=true\n")
                                 .arg(exec);
    file.write(contents.toUtf8());
    if (!file.commit()) {
        qWarning() << "Autostart: can't write" << path << file.errorString();
        return false;
    }
    return true;
}

} // namespace

bool isEnabled()
{
    if (!QFileInfo::exists(entryPath()))
        return false;
    const QHash<QString, QString> keys = readEntry();
    return keys.value(QStringLiteral("Hidden")) != QLatin1String("true")
        && keys.value(QStringLiteral("X-GNOME-Autostart-enabled")) != QLatin1String("false");
}

bool setEnabled(bool enabled)
{
    if (enabled)
        return writeEntry();
    const QString path = entryPath();
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        qWarning() << "Autostart: can't remove" << path;
        return false;
    }
    return true;
}

void refresh()
{
    if (!isEnabled())
        return;
    const QString target = firstExecArgument(readEntry().value(QStringLiteral("Exec")));
    // A newer AppImage — or a newer Nix package — usually sits in place of the
    // old one, which is still there, so the entry follows the launcher being
    // run. Only a launcher, though: a build run from its build directory must
    // not take over the installed app's entry.
    const bool fromLauncher = !appImagePath().isEmpty() || !launcherPath().isEmpty();
    const bool gone = !QFileInfo::exists(target);
    const bool anotherLauncher = fromLauncher && QFileInfo(target) != QFileInfo(executablePath());
    if (gone || anotherLauncher) {
        qInfo() << "Autostart: entry pointed at" << target << (gone ? "(missing)" : "") << "- re-pointing at"
                << executablePath();
        writeEntry();
    }
}

} // namespace Integration::Autostart
