#include <QStringList>
#include <QTest>

#include <memory>

#include "Coro.h"

namespace Tests {

namespace {

// Stands in for a request: the coroutine awaiting it goes on once the test
// opens it (at once if it is open already).
struct Gate {
    bool isOpen = false;
    std::coroutine_handle<> waiting;

    void open()
    {
        isOpen = true;
        if (const std::coroutine_handle<> h = std::exchange(waiting, { }))
            h.resume();
    }

    auto wait()
    {
        struct Awaiter {
            Gate& gate;
            bool await_ready() const noexcept { return gate.isOpen; }
            void await_suspend(std::coroutine_handle<> h) { gate.waiting = h; }
            void await_resume() const noexcept { }
        };
        return Awaiter { *this };
    }
};

Rpc::Task<void> step(Rpc::Sequence& sequence, Gate& gate, QStringList& log, QString name)
{
    const Rpc::Sequence::Turn turn = co_await sequence.turn();
    log.append(name + QStringLiteral(" sent"));
    co_await gate.wait();
    log.append(name + QStringLiteral(" done"));
}

} // namespace

class SequenceTest : public QObject {
    Q_OBJECT

private slots:
    void turnsGoInTheOrderAsked()
    {
        Rpc::Sequence sequence;
        Gate gates[3];
        QStringList log;
        for (int i = 0; i < 3; ++i)
            step(sequence, gates[i], log, QString::number(i)).detach();
        QCOMPARE(log, QStringList { QStringLiteral("0 sent") });

        // The later ones are done before the first: still nothing sent.
        gates[2].open();
        gates[1].open();
        QCOMPARE(log, QStringList { QStringLiteral("0 sent") });

        gates[0].open();
        QCOMPARE(log,
            (QStringList { QStringLiteral("0 sent"), QStringLiteral("0 done"), QStringLiteral("1 sent"),
                QStringLiteral("1 done"), QStringLiteral("2 sent"), QStringLiteral("2 done") }));
    }

    void aFreeSequenceGivesTheTurnAtOnce()
    {
        Rpc::Sequence sequence;
        Gate gate;
        gate.open();
        QStringList log;
        step(sequence, gate, log, QStringLiteral("a")).detach();
        step(sequence, gate, log, QStringLiteral("b")).detach();
        QCOMPARE(log,
            (QStringList { QStringLiteral("a sent"), QStringLiteral("a done"), QStringLiteral("b sent"),
                QStringLiteral("b done") }));
    }

    // Its owner gone, nothing waiting is resumed into it; the turn being
    // taken ends without touching the Sequence.
    void aTurnMayOutliveItsSequence()
    {
        auto sequence = std::make_unique<Rpc::Sequence>();
        Gate first;
        Gate second;
        second.open();
        QStringList log;
        step(*sequence, first, log, QStringLiteral("a")).detach();
        step(*sequence, second, log, QStringLiteral("b")).detach();
        sequence.reset();

        first.open();
        QCOMPARE(log, (QStringList { QStringLiteral("a sent"), QStringLiteral("a done") }));
    }
};

QObject* makeSequenceTest() { return new SequenceTest; }

} // namespace Tests

#include "SequenceTest.moc"
