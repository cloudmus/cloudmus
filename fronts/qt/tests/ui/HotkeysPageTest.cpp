#include <QCheckBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

#include "Registry.h"
#include "Settings.h"
#include "Settings/HotkeysPage.h"
#include "TestSupport.h"

namespace Tests {

class HotkeysPageTest : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        settings_ = std::make_unique<Config::Settings>(dir_.filePath(QStringLiteral("config.ini")));
        registry_ = std::make_unique<Hotkeys::Registry>(*settings_);
        page_ = std::make_unique<Ui::Settings::HotkeysPage>(*registry_);
        widget_ = std::make_unique<QWidget>();
        page_->createWidget(widget_.get());
    }

    void cleanup()
    {
        page_.reset();
        widget_.reset();
        registry_.reset();
        settings_.reset();
        QFile::remove(dir_.filePath(QStringLiteral("config.ini")));
    }

    void startsAsTheRegistryHasIt()
    {
        QVERIFY(!page_->isDirty());
        QCOMPARE(edits().size(), Hotkeys::actions().size());
        QCOMPARE(edits().first()->keySequence(), registry_->bindings().first().key);
    }

    void aWrappedDescriptionGetsTheHeightItNeeds()
    {
        // Narrow, as with a long translation: the descriptions wrap.
        widget_->resize(420, 800);
        widget_->show();
        QTest::qWait(50);
        int wrapped = 0;
        for (QLabel* label : widget_->findChildren<QLabel*>()) {
            if (!label->wordWrap() || label->width() <= 0)
                continue;
            QVERIFY2(label->height() >= label->heightForWidth(label->width()), qPrintable(label->text()));
            wrapped += label->heightForWidth(label->width()) > label->fontMetrics().height() * 3 / 2;
        }
        QVERIFY(wrapped > 0);
    }

    void aChangedKeyIsSavedOnApply()
    {
        edits().at(1)->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+J")));
        QVERIFY(page_->isDirty());
        QVERIFY(await(page_->apply()));
        QCOMPARE(registry_->bindings().at(1).key, QKeySequence(QStringLiteral("Ctrl+Alt+J")));
        QVERIFY(!page_->isDirty());
    }

    void theSameKeyTwiceIsRefused()
    {
        const QKeySequence taken = registry_->bindings().at(2).key;
        edits().at(1)->setKeySequence(taken);
        QVERIFY(!await(page_->apply()));
        QVERIFY(registry_->bindings().at(1).key != taken);
        QVERIFY(page_->isDirty());
    }

    void aKeyChangedInTheDesktopShowsUnlessTheUserEditedIt()
    {
        // Row 1 is being edited; row 2 isn't.
        edits().at(1)->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+J")));
        const QKeySequence desktopKey(QStringLiteral("Ctrl+Alt+Shift+F9"));
        registry_->adoptSystemKey(Hotkeys::info(Hotkeys::Action::Next).id, desktopKey);
        registry_->adoptSystemKey(Hotkeys::info(Hotkeys::Action::PlayPause).id, desktopKey);
        QCOMPARE(edits().at(2)->keySequence(), desktopKey);
        QCOMPARE(edits().at(1)->keySequence(), QKeySequence(QStringLiteral("Ctrl+Alt+J")));
    }

    void anActionCanBeResetOnItsOwn()
    {
        const QKeySequence defaultKey = registry_->bindings().at(1).key;
        // Reset is for a row that differs from its defaults.
        const auto resets = [this]() {
            QList<QPushButton*> result;
            for (QPushButton* button : widget_->findChildren<QPushButton*>()) {
                if (button->text() == QStringLiteral("Reset"))
                    result.append(button);
            }
            return result;
        };
        QVERIFY(!resets().at(1)->isEnabled());

        edits().at(1)->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+J")));
        edits().at(2)->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+K")));
        QVERIFY(resets().at(1)->isEnabled());

        resets().at(1)->click();
        QCOMPARE(edits().at(1)->keySequence(), defaultKey);
        // The other row keeps its edit.
        QCOMPARE(edits().at(2)->keySequence(), QKeySequence(QStringLiteral("Ctrl+Alt+K")));
        QVERIFY(!resets().at(1)->isEnabled());
        QVERIFY(resets().at(2)->isEnabled());
    }

    void theFilterKeepsRowsByActionOrShortcut()
    {
        auto* filter = widget_->findChild<QLineEdit*>();
        const auto shown = [this]() {
            int count = 0;
            for (QKeySequenceEdit* edit : edits())
                count += edit->isHidden() ? 0 : 1;
            return count;
        };
        QCOMPARE(shown(), Hotkeys::actions().size());

        filter->setText(QStringLiteral("volume"));
        QTRY_COMPARE(shown(), 2); // up and down

        // By key, written as the user likes: only Show player has Ctrl+Alt+Shift+M.
        filter->setText(QStringLiteral("alt shift m"));
        QTRY_COMPARE(shown(), 1);

        filter->setText(QStringLiteral("no such thing"));
        QTRY_COMPARE(shown(), 0);

        filter->clear();
        QTRY_COMPARE(shown(), Hotkeys::actions().size());
    }

    void theFilterWaitsForTypingToPause()
    {
        auto* filter = widget_->findChild<QLineEdit*>();
        const auto shown = [this]() {
            int count = 0;
            for (QKeySequenceEdit* edit : edits())
                count += edit->isHidden() ? 0 : 1;
            return count;
        };
        filter->setText(QStringLiteral("v"));
        filter->setText(QStringLiteral("vo"));
        // Not yet: nothing is filtered while keys keep coming.
        QCOMPARE(shown(), Hotkeys::actions().size());
        QTRY_VERIFY(shown() < Hotkeys::actions().size());
    }

    void actionsWithoutNoticesOrSoundsHaveNoBoxForThem()
    {
        const auto checks = widget_->findChildren<QCheckBox*>();
        int visible = 0;
        for (QCheckBox* check : checks) {
            if (!check->isHidden())
                ++visible;
        }
        int withNotice = 0;
        int withSound = 0;
        for (const Hotkeys::ActionInfo& info : Hotkeys::actions()) {
            withNotice += info.hasNotice ? 1 : 0;
            withSound += info.hasSound ? 1 : 0;
        }
        // A "global" box for every action, "notify" and "sound" ones for
        // those that can.
        QCOMPARE(visible, Hotkeys::actions().size() + withNotice + withSound);
    }

private:
    QList<QKeySequenceEdit*> edits() const { return widget_->findChildren<QKeySequenceEdit*>(); }

    QTemporaryDir dir_;
    std::unique_ptr<Config::Settings> settings_;
    std::unique_ptr<Hotkeys::Registry> registry_;
    std::unique_ptr<Ui::Settings::HotkeysPage> page_;
    std::unique_ptr<QWidget> widget_;
};

QObject* makeHotkeysPageTest() { return new HotkeysPageTest; }

} // namespace Tests

#include "HotkeysPageTest.moc"
