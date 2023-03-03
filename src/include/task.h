//
// Created by dreamHuang on 2023/2/25.
//

#ifndef HUCORO_TASK_H
#define HUCORO_TASK_H

#include "config.h"
#include "exception.h"
#include <exception>
#include <variant>


namespace hucoro {
/// task<T> represent a lazily async task that can produce a result of type T.
/// First, task<T> can be used as a returned type of coroutine
/// (which means there is a corresponding promise_type defined in it.)
/// Second, task<T> can be co_awaited and it has to be co_awaited to start execution (i.e. lazily)
template<typename T>
class Task;

/// Promise type of Task<T> to support Task as return type of coroutine function
namespace detail {
    template<typename T>
    class TaskAwaiterBase;
    class TaskPromiseBase {
        template<typename T>
        friend class TaskAwaiterBase;

    public:
        std::suspend_always initial_suspend() noexcept { return {}; }

        class FinalAwaitable {
        public:
            FinalAwaitable(std::coroutine_handle<> awaiting_coroutine) noexcept
                : awaiting_coroutine_(awaiting_coroutine) {}
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend([[maybe_unused]] std::coroutine_handle<> _handle) noexcept {
                if (awaiting_coroutine_) { return awaiting_coroutine_; }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}

        private:
            std::coroutine_handle<> awaiting_coroutine_;
        };

        FinalAwaitable final_suspend() noexcept { return {awaiting_coroutine_}; }

    private:
        // The handle of the coroutine which executes the `co_await task;`.
        // `this` promise is the promise of `task` in the above.
        std::coroutine_handle<> awaiting_coroutine_ = nullptr;
    };

    template<typename T>
    class TaskPromise final : public TaskPromiseBase {
        using coroutine_handle_t = std::coroutine_handle<TaskPromise<T>>;

    public:
        Task<T> get_return_object() noexcept { return Task{coroutine_handle_t::from_promise(*this)}; }

        template<typename U>
        void return_value(U&& result) noexcept(noexcept(result_ = std::forward<T>(result))) {
            result_ = std::forward<U>(result);
        }
        void unhandled_exception() noexcept { result_ = std::current_exception(); }

        T& result() & {
            switch (result_.index()) {
                case 0:
                    throw HuCoroGeneralErr{"No result for task, please co_await it first"};
                    break;
                case 1:
                    return std::get<1>(result_);
                case 2:
                    std::rethrow_exception(std::get<2>(result_));
                default:
                    __builtin_unreachable();
            }
        }

        T&& result() && {
            switch (result_.index()) {
                case 0:
                    throw HuCoroGeneralErr{"No result for task, please co_await it first"};
                    break;
                case 1:
                    return std::move(std::get<1>(result_));
                case 2:
                    std::rethrow_exception(std::get<2>(result_));
                default:
                    __builtin_unreachable();
            }
        }

    private:
        std::variant<std::monostate, T, std::exception_ptr> result_;
    };

    /// the lvalue ref version of TaskPromise should be handled separately,
    /// since the `std::variant` cannot accept lvalue reference
    template<typename T>
    class TaskPromise<T&> final : public TaskPromiseBase {
        using coroutine_handle_t = std::coroutine_handle<TaskPromise<T&>>;

    public:
        Task<T&> get_return_object() noexcept { return Task{coroutine_handle_t::from_promise(*this)}; }

        void return_value(T& result) noexcept { result_ = std::addressof(result); }
        void unhandled_exception() noexcept { result_ = std::current_exception(); }

        T& result() {
            switch (result_.index()) {
                case 0:
                    throw HuCoroGeneralErr{"No result for Task, please co_await it first"};
                case 1:
                    return *std::get<1>(result_);
                case 2:
                    std::rethrow_exception(std::get<2>(result_));
                default:
                    __builtin_unreachable();
            }
        }

    private:
        std::variant<std::monostate, T*, std::exception_ptr> result_;
    };

}// namespace detail

/// Awaiter type of Task<T> to support Task to be `co_await`ed.
namespace detail {

    template<typename T>
    class TaskAwaiterBase {
        using coroutine_handle_t = std::coroutine_handle<detail::TaskPromise<T>>;

    public:
        explicit TaskAwaiterBase(coroutine_handle_t handle) noexcept : task_coroutine_(handle) {}
        bool await_ready() noexcept { return task_coroutine_.done(); }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> await_coroutine) noexcept {
            task_coroutine_.promise().awaiting_coroutine_ = await_coroutine;
            return task_coroutine_;
        }

    protected:
        coroutine_handle_t task_coroutine_;
    };
}// namespace detail

template<typename T>
class [[nodiscard]] Task {
    using coroutine_handle_t = std::coroutine_handle<detail::TaskPromise<T>>;

public:
    using promise_type = detail::TaskPromise<T>;
    Task(coroutine_handle_t task_coroutine) noexcept : task_coroutine_(task_coroutine) {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) { task_coroutine_ = std::exchange(other.task_coroutine_, nullptr); }
    Task& operator=(Task&& other) {
        if (this != &other) {
            if (task_coroutine_ != nullptr) { task_coroutine_.destroy(); }
            task_coroutine_ = other.task_coroutine_;
            other.task_coroutine_ = nullptr;
        }
    }

    ~Task() {
        if (task_coroutine_ != nullptr) { task_coroutine_.destroy(); }
    }

    auto operator co_await() & noexcept {

        class TaskAwaiter : public detail::TaskAwaiterBase<T> {
        public:
            using detail::TaskAwaiterBase<T>::TaskAwaiterBase;
            decltype(auto) await_resume() { return this->task_coroutine_.promise().result(); }
        };

        return TaskAwaiter{task_coroutine_};
    }

    auto operator co_await() && noexcept {
        class TaskAwaiter : public detail::TaskAwaiterBase<T> {
        public:
            using detail::TaskAwaiterBase<T>::TaskAwaiterBase;
            decltype(auto) await_resume() { return std::move(this->task_coroutine_.promise()).result(); }
        };
        return TaskAwaiter{task_coroutine_};
    }

private:
    // the async coroutine behind the task (i.e. the function whose return type is `Task<T>`)
    coroutine_handle_t task_coroutine_;
};

namespace detail {
    /// the void version of TaskPromise should handled separately,
    /// since the `std::variant` cannot accept void and we cannot co_return a value in `TaskPromise<void>`
    template<>
    class TaskPromise<void> final : public TaskPromiseBase {

    public:
        Task<void> get_return_object() {
            using coroutine_handle_t = std::coroutine_handle<TaskPromise<void>>;
            return Task{coroutine_handle_t::from_promise(*this)};
        }

        void return_void() noexcept {}

        void unhandled_exception() { exception_ptr_ = std::current_exception(); }

        void result() const {
            if (exception_ptr_) { std::rethrow_exception(exception_ptr_); }
        }

    private:
        std::exception_ptr exception_ptr_ = nullptr;
    };
}// namespace detail

}// namespace hucoro

#endif//HUCORO_TASK_H
