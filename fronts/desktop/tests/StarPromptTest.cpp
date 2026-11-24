#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "Analytics.h"
#include "Settings.h"
#include "StarPrompt.h"

namespace Tests {

class StarPromptTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;

    QString configPath() const { return dir_.filePath(QStringLiteral("config.ini")); }

private slots:
    void init() { QFile::remove(configPath()); }

    void dueOnTheThirdDistinctDay()
    {
        Config::Settings settings(configPath());
        App::Analytics analytics(settings);
        App::StarPrompt prompt(settings, analytics);
        QSignalSpy due(&prompt, &App::StarPrompt::due);
        const QDate day(2026, 10, 1);

        prompt.recordUsage(day);
        prompt.recordUsage(day); // the same day again doesn't count
        prompt.recordUsage(day.addDays(1));
        QVERIFY(!prompt.isDue());
        QCOMPARE(due.count(), 0);

        prompt.recordUsage(day.addDays(5)); // days needn't be in a row
        QVERIFY(prompt.isDue());
        QCOMPARE(due.count(), 1);
    }

    void countSurvivesARestart()
    {
        const QDate day(2026, 10, 1);
        {
            Config::Settings settings(configPath());
            App::Analytics analytics(settings);
            App::StarPrompt prompt(settings, analytics);
            prompt.recordUsage(day);
            prompt.recordUsage(day.addDays(1));
        }
        Config::Settings settings(configPath());
        App::Analytics analytics(settings);
        App::StarPrompt prompt(settings, analytics);
        prompt.recordUsage(day.addDays(1));
        QVERIFY(!prompt.isDue());
        prompt.recordUsage(day.addDays(2));
        QVERIFY(prompt.isDue());
    }

    void neverAgainOnceDone()
    {
        Config::Settings settings(configPath());
        App::Analytics analytics(settings);
        App::StarPrompt prompt(settings, analytics);
        const QDate day(2026, 10, 1);
        for (int i = 0; i < App::StarPrompt::kDaysBeforeAsking; ++i)
            prompt.recordUsage(day.addDays(i));
        QVERIFY(prompt.isDue());

        prompt.markShown();
        QSignalSpy due(&prompt, &App::StarPrompt::due);
        prompt.recordUsage(day.addDays(10));
        QVERIFY(!prompt.isDue());
        QCOMPARE(due.count(), 0);
    }
};

QObject* makeStarPromptTest() { return new StarPromptTest; }

} // namespace Tests

#include "StarPromptTest.moc"
