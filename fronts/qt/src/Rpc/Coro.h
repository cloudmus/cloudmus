#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace Rpc {

namespace Detail {

// Shared final-suspend behavior for both Task<T> specializations: resume
// whoever is co_awaiting this task if there is one; otherwise, if the task
// was detach()ed (fire-and-forget from a non-coroutine context, e.g. a Qt
// slot), destroy the now-finished frame here rather than leaving it to a
// Task object that may no longer exist.
struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept
    {
        auto& promise = h.promise();
        if (promise.continuation) {
            return promise.continuation;
        }
        if (promise.detached) {
            h.destroy();
        }
        return std::noop_coroutine();
    }

    void await_resume() const noexcept { }
};

} // namespace Detail

// A minimal hand-written C++20 coroutine task type (no QCoro dependency,
// per fronts/qt/AGENTS.md / the project's "Qt + stdlib only" rule) — lazy
// (doesn't run until awaited or explicitly started), single-await,
// move-only. Two call patterns:
//   - `co_await someTask()` from inside another coroutine.
//   - `someTask().detach()` to fire-and-forget from a plain Qt slot; the
//     coroutine frame cleans itself up on completion (see FinalAwaiter)
//     instead of being destroyed early by a Task destructor going out of
//     scope while the coroutine is still suspended mid-flight.
template <typename T>
class Task {
public:
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;
        bool detached = false;

        Task get_return_object() { return Task { std::coroutine_handle<promise_type>::from_promise(*this) }; }
        std::suspend_always initial_suspend() noexcept { return { }; }
        Detail::FinalAwaiter final_suspend() noexcept { return { }; }
        void return_value(T v) { value = std::move(v); }
        void unhandled_exception() { exception = std::current_exception(); }
    };

    explicit Task(std::coroutine_handle<promise_type> h)
        : handle_(h)
    {
    }
    Task(Task&& other) noexcept
        : handle_(std::exchange(other.handle_, { }))
    {
    }
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, { });
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { reset(); }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting)
    {
        handle_.promise().continuation = awaiting;
        return handle_;
    }
    T await_resume()
    {
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }
        return std::move(*handle_.promise().value);
    }

    // Starts the coroutine running without anything awaiting it, and
    // arranges for its frame to be cleaned up on its own completion. This
    // Task object no longer owns the handle afterward.
    void detach() &&
    {
        if (!handle_)
            return;
        auto h = std::exchange(handle_, { });
        h.promise().detached = true;
        h.resume();
    }

private:
    void reset()
    {
        if (handle_)
            handle_.destroy();
        handle_ = { };
    }

    std::coroutine_handle<promise_type> handle_;
};

// Specialization for coroutines with no result value.
template <>
class Task<void> {
public:
    struct promise_type {
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;
        bool detached = false;

        Task get_return_object() { return Task { std::coroutine_handle<promise_type>::from_promise(*this) }; }
        std::suspend_always initial_suspend() noexcept { return { }; }
        Detail::FinalAwaiter final_suspend() noexcept { return { }; }
        void return_void() { }
        void unhandled_exception() { exception = std::current_exception(); }
    };

    explicit Task(std::coroutine_handle<promise_type> h)
        : handle_(h)
    {
    }
    Task(Task&& other) noexcept
        : handle_(std::exchange(other.handle_, { }))
    {
    }
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, { });
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { reset(); }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting)
    {
        handle_.promise().continuation = awaiting;
        return handle_;
    }
    void await_resume()
    {
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }
    }

    void detach() &&
    {
        if (!handle_)
            return;
        auto h = std::exchange(handle_, { });
        h.promise().detached = true;
        h.resume();
    }

private:
    void reset()
    {
        if (handle_)
            handle_.destroy();
        handle_ = { };
    }

    std::coroutine_handle<promise_type> handle_;
};

} // namespace Rpc
