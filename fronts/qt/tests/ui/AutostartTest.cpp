#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "Autostart.h"

namespace Tests {

#ifdef Q_OS_LINUX

namespace {

QString entryPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/cloudmus-qt.desktop");
}

QString execLine()
{
    QFile file(entryPath());
    if (!file.open(QIODevice::ReadOnly))
        return { };
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.startsWith("Exec="))
            return QString::fromUtf8(line.mid(5));
    }
    return { };
}

void touch(const QString& path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
}

} // namespace

class AutostartTest : public QObject {
    Q_OBJECT

private:
    QByteArray savedAppImage_;

private slots:
    void init()
    {
        savedAppImage_ = qgetenv("APPIMAGE");
        Integration::Autostart::setEnabled(false);
    }

    void cleanup()
    {
        Integration::Autostart::setEnabled(false);
        if (savedAppImage_.isEmpty())
            qunsetenv("APPIMAGE");
        else
            qputenv("APPIMAGE", savedAppImage_);
    }

    void followsANewerAppImageKeptNextToTheOldOne()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString older = dir.filePath(QStringLiteral("CloudMus-0.6.0-x86_64.AppImage"));
        const QString newer = dir.filePath(QStringLiteral("CloudMus-0.6.1-x86_64.AppImage"));
        touch(older);
        touch(newer);

        qputenv("APPIMAGE", older.toUtf8());
        QVERIFY(Integration::Autostart::setEnabled(true));
        QVERIFY(execLine().startsWith(older));

        qputenv("APPIMAGE", newer.toUtf8());
        Integration::Autostart::refresh();
        QCOMPARE(
            execLine(), newer + QLatin1Char(' ') + QLatin1String(Integration::Autostart::kLaunchedAtLoginArgument));
        QVERIFY(Integration::Autostart::isEnabled());
    }

    void followsThisBinaryWhenTheNamedOneIsGone()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString gone = dir.filePath(QStringLiteral("CloudMus-0.6.0-x86_64.AppImage"));
        touch(gone);
        qputenv("APPIMAGE", gone.toUtf8());
        QVERIFY(Integration::Autostart::setEnabled(true));
        QVERIFY(QFile::remove(gone));

        qunsetenv("APPIMAGE");
        Integration::Autostart::refresh();
        QVERIFY(execLine().startsWith(QCoreApplication::applicationFilePath()));
    }

    void leavesAnExistingEntryAloneOutsideAnAppImage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString installed = dir.filePath(QStringLiteral("CloudMus-0.6.0-x86_64.AppImage"));
        touch(installed);
        qputenv("APPIMAGE", installed.toUtf8());
        QVERIFY(Integration::Autostart::setEnabled(true));
        const QString before = execLine();

        // A build run from its build directory isn't the installed app.
        qunsetenv("APPIMAGE");
        Integration::Autostart::refresh();
        QCOMPARE(execLine(), before);
    }

    void doesNotCreateAnEntryTheUserHasNotAskedFor()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString appImage = dir.filePath(QStringLiteral("CloudMus-0.6.1-x86_64.AppImage"));
        touch(appImage);
        qputenv("APPIMAGE", appImage.toUtf8());

        Integration::Autostart::refresh();
        QVERIFY(!QFile::exists(entryPath()));
        QVERIFY(!Integration::Autostart::isEnabled());
    }
};

QObject* makeAutostartTest() { return new AutostartTest; }

#elif defined(Q_OS_MACOS)

namespace {

QString agentPath(const QString& home)
{
    return home + QStringLiteral("/Library/LaunchAgents/io.github.cloudmus.CloudMus.autostart.plist");
}

QStringList programArguments(const QString& home)
{
    QSettings agent(agentPath(home), QSettings::NativeFormat);
    return agent.value(QStringLiteral("ProgramArguments")).toStringList();
}

} // namespace

// The agent goes to $HOME/Library/LaunchAgents: HOME points at a temporary
// folder, never at the real one.
class AutostartTest : public QObject {
    Q_OBJECT

private:
    QByteArray savedHome_;
    QTemporaryDir home_;

private slots:
    void init()
    {
        QVERIFY(home_.isValid());
        savedHome_ = qgetenv("HOME");
        qputenv("HOME", home_.path().toUtf8());
    }

    void cleanup()
    {
        Integration::Autostart::setEnabled(false);
        qputenv("HOME", savedHome_);
    }

    void writesAnAgentStartingThisBinaryAtLogin()
    {
        QVERIFY(!Integration::Autostart::isEnabled());
        QVERIFY(Integration::Autostart::setEnabled(true));
        QVERIFY(Integration::Autostart::isEnabled());
        QCOMPARE(programArguments(home_.path()),
            QStringList({ QCoreApplication::applicationFilePath(),
                QLatin1String(Integration::Autostart::kLaunchedAtLoginArgument) }));

        QVERIFY(Integration::Autostart::setEnabled(false));
        QVERIFY(!QFile::exists(agentPath(home_.path())));
        QVERIFY(!Integration::Autostart::isEnabled());
    }

    void followsThisBinaryWhenTheNamedOneIsGone()
    {
        QVERIFY(Integration::Autostart::setEnabled(true));
        {
            QSettings agent(agentPath(home_.path()), QSettings::NativeFormat);
            agent.setValue(QStringLiteral("ProgramArguments"),
                QStringList({ home_.filePath(QStringLiteral("Gone.app/Contents/MacOS/CloudMus")),
                    QLatin1String(Integration::Autostart::kLaunchedAtLoginArgument) }));
        }
        Integration::Autostart::refresh();
        QCOMPARE(programArguments(home_.path()).value(0), QCoreApplication::applicationFilePath());
    }

    void leavesAnExistingAgentAlone()
    {
        QTemporaryDir installed;
        QVERIFY(installed.isValid());
        const QString other = installed.filePath(QStringLiteral("CloudMus"));
        {
            QFile file(other);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(Integration::Autostart::setEnabled(true));
        {
            QSettings agent(agentPath(home_.path()), QSettings::NativeFormat);
            agent.setValue(QStringLiteral("ProgramArguments"), QStringList({ other }));
        }
        // A build run from its build directory isn't the installed app.
        Integration::Autostart::refresh();
        QCOMPARE(programArguments(home_.path()), QStringList({ other }));
    }

    void doesNotCreateAnAgentTheUserHasNotAskedFor()
    {
        Integration::Autostart::refresh();
        QVERIFY(!QFile::exists(agentPath(home_.path())));
    }
};

QObject* makeAutostartTest() { return new AutostartTest; }

#else

// Autostart on Windows is a registry value; nothing here to test without
// touching the real one.
QObject* makeAutostartTest() { return new QObject; }

#endif

} // namespace Tests

#include "AutostartTest.moc"
