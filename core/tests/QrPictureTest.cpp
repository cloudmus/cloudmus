#include <QImage>
#include <QTest>

#include "QrPicture.h"

namespace Tests {

class QrPictureTest : public QObject {
    Q_OBJECT

private slots:
    void isRoundedBlackOnWhiteAtTheSizeAsked()
    {
        const QImage image = Qr::picture(QStringLiteral("https://ya.ru/device"), 240);
        QCOMPARE(image.size(), QSize(240, 240));
        // A rounded square: nothing in the very corner, white in the blank
        // margin a scanner needs.
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
        QCOMPARE(image.pixel(120, 2), qRgb(255, 255, 255));
        // The code is 25 modules and the margins 3 each; the middle of the
        // first finder pattern is solid black.
        const int middle = qRound((3 + 3.5) * 240 / 31.0);
        QCOMPARE(image.pixel(middle, middle), qRgb(0, 0, 0));
        if (const QString shots = qEnvironmentVariable("CLOUDMUS_QR_SHOTS"); !shots.isEmpty())
            image.save(shots + QStringLiteral("/qr.png"));
    }

    void keepsAtLeastAPixelPerModule() { QCOMPARE(Qr::picture(QStringLiteral("a"), 1).width(), 21 + 6); }

    void isNullForTextTooLongForACode() { QVERIFY(Qr::picture(QString(8000, QLatin1Char('x')), 240).isNull()); }
};

QObject* makeQrPictureTest() { return new QrPictureTest; }

} // namespace Tests

#include "QrPictureTest.moc"
