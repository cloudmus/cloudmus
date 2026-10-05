#include <QTest>

#include "AboutDialog.h"
#include "AudioPulse.h"
#include "CloudsBackdrop.h"

namespace Tests {

class AboutDialogTest : public QObject {
    Q_OBJECT

private slots:
    void theCloudsCoverTheDialogAndRunOnlyWhileItIsShown()
    {
        ViewModel::AudioPulse pulse({ [](bool) { }, []() { return QVector<Playback::LevelReading> { }; } });
        Ui::AboutDialog dialog(pulse);
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
};

QObject* makeAboutDialogTest() { return new AboutDialogTest; }

} // namespace Tests

#include "AboutDialogTest.moc"
