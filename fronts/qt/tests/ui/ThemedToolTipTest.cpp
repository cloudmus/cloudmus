#include <QApplication>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QPushButton>
#include <QTest>

#include "ThemedToolTip.h"

namespace Tests {

class ThemedToolTipTest : public QObject {
    Q_OBJECT

private slots:
    // The offline indicator's details: several lines, one of them long. They
    // show as lines, the long one wrapped — not one line across the screen.
    void severalLinesShowAsLinesAndALongOneWraps()
    {
        Ui::ThemedToolTip toolTip;
        QPushButton button(QStringLiteral("x"));
        button.show();
        QVERIFY(QTest::qWaitForWindowExposed(&button));
        const QString longLine = QStringLiteral("Yandex Music: unreachable since 01:07 (")
            + QString(QStringLiteral("word ")).repeated(40) + QStringLiteral(")");
        button.setToolTip(
            QStringLiteral("Yandex Music is unreachable\n") + longLine + QStringLiteral("\nClick to check now"));

        QHelpEvent event(QEvent::ToolTip, QPoint(1, 1), button.mapToGlobal(QPoint(1, 1)));
        QApplication::sendEvent(&button, &event);
        QWidget* popup = nullptr;
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QStringLiteral("themedToolTip"))
                popup = widget;
        }
        QVERIFY(popup != nullptr);
        QTRY_VERIFY(popup->isVisible());

        const QFontMetrics metrics(popup->font());
        // Three lines and more (the long one wraps), not one.
        QVERIFY(popup->height() > metrics.lineSpacing() * 4);
        // No wider than the wrap allows, with room for the padding and shadow.
        QVERIFY(popup->width() < metrics.averageCharWidth() * 60 + metrics.height() * 6);
        const QString shot = qEnvironmentVariable("CLOUDMUS_TOOLTIP_SHOT");
        if (!shot.isEmpty()) {
            QTest::qWait(500); // faded in
            popup->grab().save(shot);
        }
    }
};

QObject* makeThemedToolTipTest() { return new ThemedToolTipTest; }

} // namespace Tests

#include "ThemedToolTipTest.moc"
