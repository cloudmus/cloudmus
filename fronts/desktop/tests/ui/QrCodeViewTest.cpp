#include <QJsonObject>
#include <QTest>
#include <QThreadPool>
#include <QVBoxLayout>

#include "AuthCard.h"
#include "QrCodeView.h"

namespace Tests {

// The sign-in card's QR code, and the whole of the window it takes on a click.
class QrCodeViewTest : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        window_ = std::make_unique<QWidget>();
        auto* layout = new QVBoxLayout(window_.get());
        card_ = new Ui::AuthCard(Ui::AuthCard::Look::Standalone, window_.get());
        layout->addWidget(card_);
        layout->addStretch(1);
        window_->resize(700, 600);
        window_->show();
        card_->showPrompt(prompt());
        QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
    }

    void cleanup() { window_.reset(); }

    void aDeviceCodeComesWithItsAddressAsAQrCode()
    {
        QVERIFY(view()->isVisible());
        QCOMPARE(view()->content(), QStringLiteral("https://ya.ru/device"));
        shot("card");
        card_->showError(QStringLiteral("expired"));
        QVERIFY(!view()->isVisible());
    }

    void aClickTakesItOverTheWindowAndEscapeBack()
    {
        QTest::mouseClick(view(), Qt::LeftButton);
        QVERIFY(view()->isExpanded());
        auto* full = overlay();
        QVERIFY(full != nullptr);
        QCOMPARE(full->geometry(), window_->rect());
        QCOMPARE(window_->focusWidget(), full);
        QTest::qWait(500);
        shot("expanded");
        // It follows the window.
        window_->resize(500, 700);
        QTRY_COMPARE(full->size(), window_->size());
        QTest::keyClick(full, Qt::Key_Escape);
        QVERIFY(!view()->isExpanded());
        QTRY_VERIFY(overlay() == nullptr);
        QCOMPARE(window_->focusWidget(), view());
    }

    void aClickOnItTakesItBack()
    {
        QTest::mouseClick(view(), Qt::LeftButton);
        QTest::mouseClick(overlay(), Qt::LeftButton);
        QVERIFY(!view()->isExpanded());
        QTRY_VERIFY(overlay() == nullptr);
    }

    // The same prompt again (the page refreshing itself) leaves it be.
    void theSamePromptAgainKeepsItExpanded()
    {
        QTest::mouseClick(view(), Qt::LeftButton);
        card_->showPrompt(prompt());
        QTest::qWait(100);
        QVERIFY(view()->isExpanded());
    }

    void signingInTakesItAway()
    {
        QTest::mouseClick(view(), Qt::LeftButton);
        card_->hide();
        QTRY_VERIFY(overlay() == nullptr);
    }

    void goingWithItsCardLeavesNothingOver()
    {
        QTest::mouseClick(view(), Qt::LeftButton);
        delete card_;
        QVERIFY(overlay() == nullptr);
        // The picture it asked for arrives to nobody.
        QVERIFY(QThreadPool::globalInstance()->waitForDone());
        QCoreApplication::processEvents();
    }

private:
    static QJsonObject prompt()
    {
        return { { QStringLiteral("flow"), QStringLiteral("deviceCode") },
            { QStringLiteral("url"), QStringLiteral("https://ya.ru/device") },
            { QStringLiteral("code"), QStringLiteral("abc123") } };
    }

    Ui::QrCodeView* view() const { return card_->findChild<Ui::QrCodeView*>(); }

    QWidget* overlay() const { return window_->findChild<QWidget*>(QStringLiteral("qrOverlay")); }

    // For looking at it: CLOUDMUS_QR_SHOTS=<directory>.
    void shot(const char* name) const
    {
        if (const QString dir = qEnvironmentVariable("CLOUDMUS_QR_SHOTS"); !dir.isEmpty())
            window_->grab().save(dir + QStringLiteral("/desktop-qr-") + QLatin1String(name) + QStringLiteral(".png"));
    }

    std::unique_ptr<QWidget> window_;
    Ui::AuthCard* card_ = nullptr;
};

QObject* makeQrCodeViewTest() { return new QrCodeViewTest; }

} // namespace Tests

#include "QrCodeViewTest.moc"
