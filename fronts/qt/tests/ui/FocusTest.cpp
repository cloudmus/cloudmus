#include <QApplication>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QTest>
#include <QToolButton>

#include "FocusRing.h"
#include <QImage>
#include <QStyleFactory>

#include "NowPlayingBar.h"
#include "Style.h"
#include "StyleSheet.h"
#include "TabOrder.h"
#include "Tokens.h"

namespace Tests {

class FocusTest : public QObject {
    Q_OBJECT

private slots:
    void tabFollowsLayoutNotCreationOrder()
    {
        QWidget window;
        // Created c, a, b — laid out a, b, c.
        auto* c = new QPushButton(&window);
        auto* a = new QPushButton(&window);
        auto* b = new QPushButton(&window);
        auto* layout = new QHBoxLayout(&window);
        layout->addWidget(a);
        layout->addWidget(b);
        layout->addWidget(c);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        Ui::chainTabOrder(&window);
        a->setFocus(Qt::TabFocusReason);
        // What Tab reaches: the chain minus widgets that don't take focus
        // (the window itself sits in it).
        const auto nextTabStop = [](QWidget* from) {
            QWidget* w = from->nextInFocusChain();
            while (!(w->focusPolicy() & Qt::TabFocus))
                w = w->nextInFocusChain();
            return w;
        };
        QCOMPARE(nextTabStop(a), b);
        QCOMPARE(nextTabStop(b), c);
        QCOMPARE(nextTabStop(c), a);
    }

    void transportBarTabsLeftToRight()
    {
        Ui::NowPlayingBar bar;
        auto* trailing = new QToolButton(&bar);
        bar.setTrailingWidget(trailing);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        const auto buttons = bar.findChildren<QPushButton*>();
        // Chain order must agree with the horizontal position of buttons on the same row.
        QWidget* previous = nullptr;
        for (QWidget* w = bar.nextInFocusChain(); w != &bar; w = w->nextInFocusChain()) {
            if (!qobject_cast<QPushButton*>(w))
                continue;
            if (previous != nullptr && previous->y() == w->y())
                QVERIFY2(previous->x() < w->x(), "tab order jumps backwards within a row");
            previous = w;
        }
        QVERIFY(!buttons.isEmpty());
        QVERIFY(trailing->focusPolicy() & Qt::TabFocus);
        bool reached = false;
        for (QWidget* w = bar.nextInFocusChain(); w != &bar; w = w->nextInFocusChain())
            reached |= w == trailing;
        QVERIFY(reached);
    }

    void roundButtonGetsAccentRingNotASquare()
    {
        QApplication::setStyle(new Theme::CloudMusStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
        Theme::applyGlobalStyleSheet(*qApp);
        Theme::FocusRing ring;
        QWidget window;
        auto* other = new QPushButton(&window);
        auto* button = new QPushButton(&window);
        button->setProperty("variant", "icon");
        button->setFixedSize(36, 36);
        auto* layout = new QHBoxLayout(&window);
        layout->addWidget(other);
        layout->addWidget(button);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        // Focus has to move for a FocusIn (the first widget already has it).
        other->setFocus(Qt::MouseFocusReason);
        button->setFocus(Qt::TabFocusReason);
        QImage image = button->grab().toImage();
        const QColor accent = Theme::palette().accent;
        // On the ring at the left edge of the circle's middle, and not in the corner a square box would hit.
        QVERIFY(image.pixelColor(1, 18) == accent || image.pixelColor(2, 18) == accent);
        QVERIFY(image.pixelColor(1, 1) != accent);

        other->setFocus(Qt::MouseFocusReason);
        button->setFocus(Qt::MouseFocusReason);
        image = button->grab().toImage();
        QVERIFY(image.pixelColor(1, 18) != accent && image.pixelColor(2, 18) != accent);
    }

    void menuToolButtonGetsARingToo()
    {
        QApplication::setStyle(new Theme::CloudMusStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
        Theme::applyGlobalStyleSheet(*qApp);
        Theme::FocusRing ring;
        QWidget window;
        auto* other = new QPushButton(&window);
        auto* button = new QToolButton(&window);
        button->setProperty("variant", "icon");
        button->setFixedSize(36, 36);
        auto* layout = new QHBoxLayout(&window);
        layout->addWidget(other);
        layout->addWidget(button);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        other->setFocus(Qt::MouseFocusReason);
        button->setFocus(Qt::TabFocusReason);
        const QImage image = button->grab().toImage();
        const QColor accent = Theme::palette().accent;
        QVERIFY(image.pixelColor(1, 18) == accent || image.pixelColor(2, 18) == accent);
    }

    void ringShowsOnlyAfterKeyboardNavigation()
    {
        Theme::FocusRing ring;
        // The mode is app-wide: start from mouse mode, whatever ran before.
        QWidget probe;
        QMouseEvent reset(
            QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&probe, &reset);
        QPushButton button;
        button.show();
        QVERIFY(QTest::qWaitForWindowExposed(&button));

        button.setFocus(Qt::MouseFocusReason);
        QVERIFY(!Theme::focusVisible(&button));

        button.clearFocus();
        button.setFocus(Qt::TabFocusReason);
        QVERIFY(Theme::focusVisible(&button));

        QMouseEvent press(
            QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&button, &press);
        QVERIFY(!Theme::focusVisible(&button));
    }
};

QObject* makeFocusTest() { return new FocusTest; }

} // namespace Tests

#include "FocusTest.moc"
