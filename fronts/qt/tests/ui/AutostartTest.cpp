#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "Autostart.h"

namespace Tests {

#ifndef Q_OS_WIN

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

#else

// Autostart on Windows is a registry value; nothing here to test without
// touching the real one.
QObject* makeAutostartTest() { return new QObject; }

#endif

} // namespace Tests

#include "AutostartTest.moc"
