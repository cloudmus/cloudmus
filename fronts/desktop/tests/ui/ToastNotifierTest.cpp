#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTest>
#include <QWidget>

#include "ToastNotifier.h"

namespace Tests {

class ToastNotifierTest : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        anchor_ = std::make_unique<QWidget>();
        anchor_->resize(900, 600);
        anchor_->show();
        notifier_ = std::make_unique<Ui::ToastNotifier>(anchor_.get());
    }

    void cleanup()
    {
        notifier_.reset();
        anchor_.reset();
    }

    // The chevron unfolds the details, which then stay up past the usual time
    // and are what copying gives; folded again it is the short line only.
    void aToastWithDetailsUnfoldsAndCopiesWhatItShows()
    {
        const QString message = QStringLiteral("YouTube Music: no network connection");
        const QString details = QStringLiteral("YouTube Music: HTTPSConnectionPool(host='music.youtube.com')");
        notifier_->showError(message, details);
        QWidget* toast = toastWidget();
        QTRY_VERIFY(toast->isVisible());
        QTest::qWait(400); // its entrance
        const int folded = toast->height();

        QCOMPARE(copied(toast), message);

        QVERIFY(clickChevron(toast, folded));
        QVERIFY(toast->height() > folded);
        QCOMPARE(copied(toast), message + QStringLiteral("\n\n") + details);

        // Unfolded, it isn't taken away by itself.
        QTest::qWait(4600);
        QVERIFY(toast->isVisible());
        QVERIFY(!toast->testAttribute(Qt::WA_TransparentForMouseEvents));

        QVERIFY(clickChevron(toast, toast->height(), /*expectGrowth=*/false));
        QCOMPARE(toast->height(), folded);
        QCOMPARE(copied(toast), message);
    }

    void aToastWithoutDetailsHasNoChevron()
    {
        notifier_->showInfo(QStringLiteral("Added to Liked"));
        QWidget* toast = toastWidget();
        QTRY_VERIFY(toast->isVisible());
        QTest::qWait(400);
        const int height = toast->height();
        // Along the whole width but the ×: nothing unfolds.
        for (int x = 0; x < toast->width() - 60; x += 4)
            QTest::mouseClick(toast, Qt::LeftButton, Qt::NoModifier, QPoint(x, 22));
        QCOMPARE(toast->height(), height);
        QVERIFY(toast->isVisible());
    }

private:
    QWidget* toastWidget() const
    {
        const QList<QWidget*> toasts = anchor_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        return toasts.size() == 1 ? toasts.first() : nullptr;
    }

    // The toast paints its own buttons, so find the chevron by scanning the
    // first row from the left (the × is at the far right and closes it).
    bool clickChevron(QWidget* toast, int heightBefore, bool expectGrowth = true)
    {
        for (int x = toast->width() / 2; x < toast->width() - 20; x += 2) {
            QTest::mouseClick(toast, Qt::LeftButton, Qt::NoModifier, QPoint(x, 22));
            if (toast->height() != heightBefore)
                return expectGrowth ? toast->height() > heightBefore : toast->height() < heightBefore;
        }
        return false;
    }

    // What its "Copy Message" gives.
    QString copied(QWidget* toast)
    {
        QApplication::clipboard()->clear();
        QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 22), toast->mapToGlobal(QPoint(20, 22)));
        QApplication::sendEvent(toast, &event);
        for (QWidget* top : QApplication::topLevelWidgets()) {
            if (auto* menu = qobject_cast<QMenu*>(top); menu != nullptr && menu->isVisible()) {
                menu->actions().first()->trigger();
                menu->close();
                break;
            }
        }
        return QApplication::clipboard()->text();
    }

    std::unique_ptr<QWidget> anchor_;
    std::unique_ptr<Ui::ToastNotifier> notifier_;
};

QObject* makeToastNotifierTest() { return new ToastNotifierTest; }

} // namespace Tests

#include "ToastNotifierTest.moc"
