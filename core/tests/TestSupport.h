#pragma once

#include <QEventLoop>
#include <QTimer>

#include <exception>
#include <optional>

#include "Coro.h"

namespace Tests {

// Runs `task` to completion on a local event loop — the RPC calls it
// awaits are answered through the event loop — and returns its result,
// rethrowing what it threw. Gives up after `timeoutMs`.
template <typename T>
T await(Rpc::Task<T> task, int timeoutMs = 10000)
{
    QEventLoop loop;
    std::optional<T> result;
    std::exception_ptr error;
    bool done = false;
    auto runner = [&](Rpc::Task<T> inner) -> Rpc::Task<void> {
        try {
            result = co_await std::move(inner);
        } catch (...) {
            error = std::current_exception();
        }
        done = true;
        loop.quit();
    };
    runner(std::move(task)).detach();
    if (!done) {
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (!done)
        throw std::runtime_error("timed out");
    if (error)
        std::rethrow_exception(error);
    return std::move(*result);
}

inline void await(Rpc::Task<void> task, int timeoutMs = 10000)
{
    auto wrapped = [](Rpc::Task<void> inner) -> Rpc::Task<bool> {
        co_await std::move(inner);
        co_return true;
    };
    await(wrapped(std::move(task)), timeoutMs);
}

} // namespace Tests
