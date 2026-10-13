#include <QTest>

#include "Debounce.h"
#include "Throttle.h"

namespace Tests {

class TimingTest : public QObject {
    Q_OBJECT

private slots:
    void debounceRunsOnceAfterTheLastTrigger()
    {
        int runs = 0;
        Timing::Debounce debounce(50, [&runs]() { ++runs; });
        for (int i = 0; i < 5; ++i) {
            debounce.trigger();
            QTest::qWait(10);
        }
        QCOMPARE(runs, 0);
        QVERIFY(debounce.isPending());
        QTRY_COMPARE(runs, 1);
        QVERIFY(!debounce.isPending());
        QTest::qWait(100);
        QCOMPARE(runs, 1);
    }

    void debounceFlushRunsAWaitingRunNow()
    {
        int runs = 0;
        Timing::Debounce debounce(50, [&runs]() { ++runs; });
        debounce.flush(); // nothing waiting
        QCOMPARE(runs, 0);
        debounce.trigger();
        debounce.flush();
        QCOMPARE(runs, 1);
        QTest::qWait(100);
        QCOMPARE(runs, 1);
    }

    void debounceCancelDropsAWaitingRun()
    {
        int runs = 0;
        Timing::Debounce debounce(50, [&runs]() { ++runs; });
        debounce.trigger();
        debounce.cancel();
        QTest::qWait(100);
        QCOMPARE(runs, 0);
    }

    void throttleRunsAtOnceThenOncePerInterval()
    {
        int runs = 0;
        Timing::Throttle throttle(50, [&runs]() { ++runs; });
        throttle.trigger();
        QCOMPARE(runs, 1);
        throttle.trigger();
        throttle.trigger();
        QCOMPARE(runs, 1);
        // The ones held back come out as one run...
        QTRY_COMPARE(runs, 2);
        // ...which starts an interval of its own.
        throttle.trigger();
        QCOMPARE(runs, 2);
        QTRY_COMPARE(runs, 3);
        QTest::qWait(100);
        QCOMPARE(runs, 3);
        throttle.trigger();
        QCOMPARE(runs, 4);
    }

    void throttleCancelDropsAWaitingRun()
    {
        int runs = 0;
        Timing::Throttle throttle(50, [&runs]() { ++runs; });
        throttle.trigger();
        throttle.trigger();
        throttle.cancel();
        QTest::qWait(100);
        QCOMPARE(runs, 1);
        throttle.trigger();
        QCOMPARE(runs, 2);
    }
};

QObject* makeTimingTest() { return new TimingTest; }

} // namespace Tests

#include "TimingTest.moc"
