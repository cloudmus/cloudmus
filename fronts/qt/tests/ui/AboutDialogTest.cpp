#include <QAbstractButton>
#include <QFrame>
#include <QLabel>
#include <QStackedWidget>
#include <QTest>

#include "AboutDialog.h"
#include "AudioPulse.h"
#include "LogoSound.h"
#include "CloudsBackdrop.h"
#include "People.h"
#include "SpriteAnimation.h"

namespace Tests {

namespace {
// The logo's sound, never sounding: the views only.
ViewModel::LogoSound::Player silentLogo() { return { []() { }, []() { return -1.0; } }; }
}

class AboutDialogTest : public QObject {
    Q_OBJECT

private slots:
    void theCloudsCoverTheDialogAndRunOnlyWhileItIsShown()
    {
        ViewModel::AudioPulse pulse({ [](bool) { }, []() { return QVector<Playback::LevelReading> { }; } });
        ViewModel::LogoSound sound(silentLogo());
        Ui::AboutDialog dialog(pulse, sound);
        auto* backdrop = dialog.findChild<Ui::CloudsBackdrop*>();
        QVERIFY(backdrop);
        QVERIFY(!pulse.isActive());

        dialog.show();
        QTest::qWait(60);
        QCOMPARE(backdrop->geometry(), dialog.rect());
        QVERIFY(pulse.isActive());

        dialog.resize(dialog.width() + 80, dialog.height() + 40);
        QTest::qWait(30);
        QCOMPARE(backdrop->geometry(), dialog.rect());

        dialog.hide();
        QVERIFY(!pulse.isActive()); // a closed dialog costs nothing
    }

    // Authors and contributors, a card each: the name, the email, the page.
    void peopleHaveCards()
    {
        const QList<About::Person> authors = About::authors();
        const QList<About::Person> contributors = About::contributors();
        QCOMPARE(authors.size(), 1);
        QCOMPARE(authors.first().url, QStringLiteral("https://github.com/navrocky"));
        QVERIFY(contributors.size() >= 2);
        QCOMPARE(About::shownUrl(QStringLiteral("https://github.com/derfenix")), QStringLiteral("github.com/derfenix"));

        ViewModel::AudioPulse pulse({ [](bool) { }, []() { return QVector<Playback::LevelReading> { }; } });
        ViewModel::LogoSound sound(silentLogo());
        Ui::AboutDialog dialog(pulse, sound);
        dialog.show();
        const auto check = [&dialog](const QString& pageName, const QList<About::Person>& people) {
            auto* page = dialog.findChild<QWidget*>(pageName);
            QVERIFY(page);
            const QList<QFrame*> cards = page->findChildren<QFrame*>(QStringLiteral("personCard"));
            QCOMPARE(cards.size(), people.size());
            for (int i = 0; i < people.size(); ++i) {
                QStringList texts;
                for (QLabel* label : cards[i]->findChildren<QLabel*>())
                    texts.append(label->text());
                const QString all = texts.join(QLatin1Char('\n'));
                QVERIFY(all.contains(people[i].name));
                QVERIFY(all.contains(QStringLiteral("mailto:") + people[i].email));
                QVERIFY(all.contains(About::shownUrl(people[i].url)));
                // The person icon, then the lines.
                QVERIFY(!cards[i]->findChildren<QLabel*>().first()->pixmap().isNull());
            }
        };
        check(QStringLiteral("authorsPage"), authors);
        check(QStringLiteral("contributorsPage"), contributors);

        QAbstractButton* tab = nullptr;
        for (QAbstractButton* button : dialog.findChildren<QAbstractButton*>()) {
            if (button->text() == QLatin1String("Contributors"))
                tab = button;
        }
        QVERIFY(tab);
        tab->click();
        QCOMPARE(dialog.findChild<QStackedWidget*>()->currentWidget()->objectName(), QStringLiteral("contributorsPage"));
    }

    // A tab picked slides in over the one left: snapshots move meanwhile, the
    // live pages hidden, and once it's over the page picked is there to use.
    // Picked again before that, the last one picked wins.
    void tabsSlideIntoPlace()
    {
        ViewModel::AudioPulse pulse({ [](bool) { }, []() { return QVector<Playback::LevelReading> { }; } });
        ViewModel::LogoSound sound(silentLogo());
        Ui::AboutDialog dialog(pulse, sound);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto* pages = dialog.findChild<QStackedWidget*>();
        QVERIFY(pages);
        const auto tabNamed = [&dialog](const char* text) -> QAbstractButton* {
            for (QAbstractButton* button : dialog.findChildren<QAbstractButton*>()) {
                if (button->text() == QLatin1String(text))
                    return button;
            }
            return nullptr;
        };
        QVERIFY(tabNamed("License") && tabNamed("Contributors"));

        tabNamed("License")->click();
        QVERIFY(!pages->isVisible()); // the snapshots move
        QCOMPARE(pages->currentIndex(), 3);
        QTRY_VERIFY(pages->isVisible());

        tabNamed("Contributors")->click();
        QTest::qWait(60);
        tabNamed("License")->click(); // back again, halfway
        QVERIFY(!pages->isVisible());
        QTRY_VERIFY(pages->isVisible());
        QCOMPARE(pages->currentIndex(), 3);
    }

    // The window takes the height of the tab picked over a moment, not at once:
    // taller for Technologies (seven rows), then back to the short Authors.
    void theWindowGrowsAndShrinksToTheTab()
    {
        ViewModel::AudioPulse pulse({ [](bool) { }, []() { return QVector<Playback::LevelReading> { }; } });
        ViewModel::LogoSound sound(silentLogo());
        Ui::AboutDialog dialog(pulse, sound);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        const auto tabNamed = [&dialog](const char* text) -> QAbstractButton* {
            for (QAbstractButton* button : dialog.findChildren<QAbstractButton*>()) {
                if (button->text() == QLatin1String(text))
                    return button;
            }
            return nullptr;
        };
        QVERIFY(tabNamed("Technologies") && tabNamed("Authors"));
        const auto* pages = dialog.findChild<QStackedWidget*>();
        QVERIFY(pages);

        // The heights seen from the click until it settles, a few ms apart.
        const auto heightsAfterClick = [&](const char* tab) {
            QList<int> heights { dialog.height() };
            tabNamed(tab)->click();
            for (int i = 0; i < 40; ++i) {
                heights.append(dialog.height());
                QTest::qWait(10);
            }
            return heights;
        };

        const int shortHeight = dialog.height();
        const QList<int> growing = heightsAfterClick("Technologies");
        const int tallHeight = dialog.height();
        QVERIFY2(tallHeight > shortHeight + 100, "Technologies is taller than Authors");
        QTRY_VERIFY(pages->isVisible());
        // On the way: more than one step between, never back, never past.
        QSet<int> between;
        for (int i = 1; i < growing.size(); ++i) {
            QVERIFY2(growing[i] >= growing[i - 1], "grows, doesn't wobble");
            QVERIFY(growing[i] <= tallHeight);
            if (growing[i] > shortHeight && growing[i] < tallHeight)
                between.insert(growing[i]);
        }
        QVERIFY2(between.size() >= 3, qPrintable(QString::number(between.size())));
        // Settled: the minimum is the page's again, below which it can't be squeezed.
        QCOMPARE(dialog.height(), tallHeight);
        QVERIFY(dialog.minimumHeight() > 1);

        const QList<int> shrinking = heightsAfterClick("Authors");
        QTRY_COMPARE(dialog.height(), shortHeight);
        between.clear();
        for (int i = 1; i < shrinking.size(); ++i) {
            QVERIFY2(shrinking[i] <= shrinking[i - 1], "shrinks, doesn't wobble");
            if (shrinking[i] < tallHeight && shrinking[i] > shortHeight)
                between.insert(shrinking[i]);
        }
        QVERIFY2(between.size() >= 3, qPrintable(QString::number(between.size())));

        // Another tab picked before it's there: on from where it is, to the new one.
        tabNamed("Technologies")->click();
        QTest::qWait(80);
        const int midway = dialog.height();
        QVERIFY(midway > shortHeight && midway < tallHeight);
        tabNamed("Authors")->click();
        QTRY_COMPARE(dialog.height(), shortHeight);
        QTRY_VERIFY(pages->isVisible());
    }

    // Technologies, each by its logo; the AI agents wave, but only while their
    // tab is shown.
    void theTechnologiesTabHasLogosAndWavingAgents()
    {
        ViewModel::AudioPulse pulse({ [](bool) { }, []() { return QVector<Playback::LevelReading> { }; } });
        ViewModel::LogoSound sound(silentLogo());
        Ui::AboutDialog dialog(pulse, sound);
        dialog.show();
        const QList<Ui::SpriteAnimation*> mascots = dialog.findChildren<Ui::SpriteAnimation*>();
        QCOMPARE(mascots.size(), 2);
        QVERIFY(!mascots.first()->isVisible());
        QAbstractButton* tab = nullptr;
        for (QAbstractButton* button : dialog.findChildren<QAbstractButton*>()) {
            if (button->text() == QLatin1String("Technologies"))
                tab = button;
        }
        QVERIFY(tab);
        tab->click();
        QTRY_VERIFY(mascots.first()->isVisible()); // once it has slid in
        const int first = mascots.first()->frame();
        QTRY_VERIFY(mascots.first()->frame() != first);
        auto* shown = dialog.findChild<QStackedWidget*>()->currentWidget();
        bool claude = false;
        bool codex = false;
        for (QLabel* label : shown->findChildren<QLabel*>()) {
            claude = claude || label->text().contains(QLatin1String(">Claude</a>"));
            codex = codex || label->text().contains(QLatin1String(">Codex</a>"));
        }
        QVERIFY(claude && codex);
        int logos = 0;
        for (QLabel* label : shown->findChildren<QLabel*>(QStringLiteral("technologyLogo"))) {
            QVERIFY(!label->pixmap().isNull());
            ++logos;
        }
        QCOMPARE(logos, 5);
    }
};

QObject* makeAboutDialogTest() { return new AboutDialogTest; }

} // namespace Tests

#include "AboutDialogTest.moc"
