#include <QSignalSpy>
#include <QTest>

#include "SingleInstance.h"

Q_DECLARE_METATYPE(App::SingleInstance::Launch)

namespace Tests {

class SingleInstanceTest : public QObject {
    Q_OBJECT

private:
    // Not the app's own key: the tests may run next to a real instance.
    static QString key() { return QStringLiteral("cloudmus-qt-test-single-instance"); }

private slots:
    void initTestCase() { qRegisterMetaType<App::SingleInstance::Launch>(); }

    void secondLaunchHandsOverToTheFirst()
    {
        App::SingleInstance first(key());
        QSignalSpy launched(&first, &App::SingleInstance::launched);
        QVERIFY(first.claim({ }));

        App::SingleInstance second(key());
        const App::SingleInstance::Launch launch { { QStringLiteral("--autostart"), QStringLiteral("a.flac") },
            QStringLiteral("/music"), QStringLiteral("token") };
        QVERIFY(!second.claim(launch));

        QVERIFY(launched.wait(2000));
        const auto received = launched.takeFirst().at(0).value<App::SingleInstance::Launch>();
        QCOMPARE(received.arguments, launch.arguments);
        QCOMPARE(received.workingDirectory, launch.workingDirectory);
        QCOMPARE(received.activationToken, launch.activationToken);
    }

    void freeAgainOnceTheFirstIsGone()
    {
        {
            App::SingleInstance first(key());
            QVERIFY(first.claim({ }));
        }
        App::SingleInstance next(key());
        QVERIFY(next.claim({ }));
    }

    void unknownFieldsAreIgnored()
    {
        const auto launch = App::SingleInstance::decode(R"({"arguments":["x"],"newThing":1})"
                                                        "\n");
        QCOMPARE(launch.arguments, QStringList { QStringLiteral("x") });
        QVERIFY(launch.workingDirectory.isEmpty());
        QVERIFY(launch.activationToken.isEmpty());
    }
};

QObject* makeSingleInstanceTest() { return new SingleInstanceTest; }

} // namespace Tests

#include "SingleInstanceTest.moc"
