//
// Created by dreamHuang on 2023/3/3.
//

#ifndef HUCORO_SPAWN_TASK_H
#define HUCORO_SPAWN_TASK_H
#include "config.h"
#include "exception.h"
#include "hucoro_traits.h"
#include <atomic>
#include <exception>
#include <type_traits>
#include <utility>
#include <variant>

namespace hucoro {
template<typename Result>
class JoinHandleBase;

template<typename Result>
class JoinHandle;

template<typename Result>
class SpawnTaskPromise;

class SpawnTask;
namespace {
    /// Ths state of spawn task promise
    enum class State {
        // Init (we can simply destory it when ref count = 0 in this state)
        INIT = 0,
        // running state (we cannot destory it in this state even if ref count = 0 because it is running)
        IN_PROGRESS,
        // there is a coroutine waiting for the spawn to end
        WAITING_TO_RESUME,
        // The spawn task is already finish
        FINISH,
    };
}// namespace

class SpawnTaskPromiseState {
public:
    SpawnTaskPromiseState() : state_(State::INIT) {}
    auto final_suspend() noexcept {}

    void incr_ref() { val_.fetch_add(1, std::memory_order_release); }

    size_t decr_ref() { return val_.fetch_sub(1, std::memory_order_acq_rel) - 1; }

    auto ref_count() const { return val_.load(std::memory_order_acquire); }

    State state() const { return state_.load(std::memory_order_acquire); }

    void set_awaiting_coroutine(std::coroutine_handle<> handle) { awaiting_coroutine_ = handle; }

    std::atomic<State> state_;
    std::coroutine_handle<> awaiting_coroutine_ = nullptr;
    // reference count
    std::atomic<size_t> val_;
};


namespace detail {
    template<typename Result>
    struct SpawnTaskFinalAwaiter {
        using coroutine_handle_t = std::coroutine_handle<SpawnTaskPromise<Result>>;
        SpawnTaskFinalAwaiter(coroutine_handle_t handle) : coroutine_(handle) {}

        bool await_ready() noexcept {
            auto& state = coroutine_.promise().state();
            return state.state() != State::WAITING_TO_RESUME && state.ref_count() == 0;
        }

        std::coroutine_handle<> await_suspend(coroutine_handle_t handle) noexcept {
            auto& promise = handle.promise();
            SpawnTaskPromiseState& state = promise.state();
            State prev_state = state.state_.exchange(State::FINISH, std::memory_order_acq_rel);
            if (prev_state == State::WAITING_TO_RESUME) { return state.awaiting_coroutine_; }
            if (state.val_.load(std::memory_order_acquire) == 0) {
                // no reference to this task
                // so it can be destroy (we do this by continue current coroutine)
                return handle;
            }
            // we come here because there is at least one handle to this coroutine
            // (e.g. JoinHandle) and no awaiting coroutine exist.
            //
            // so return to caller/resumer and keep the result in SpawnTaskPromise
            return std::noop_coroutine();
        }

        void await_resume() noexcept {}

        coroutine_handle_t coroutine_;
    };

}// namespace detail

template<typename Result>
class SpawnTaskPromise {
    static_assert(std::atomic<State>::is_always_lock_free);

public:
    std::pair<JoinHandle<Result>, SpawnTask> get_return_object();
    std::suspend_always initial_suspend() noexcept { return {}; }
    void unhandled_exception() { result_ = std::current_exception(); }
    detail::SpawnTaskFinalAwaiter<Result> final_suspend() noexcept {
        return {std::coroutine_handle<SpawnTaskPromise<Result>>::from_promise(*this)};
    }

    template<typename U>
    void return_value(U&& val) {
        result_.template emplace<1>(std::forward<U>(val));
    }

    SpawnTaskPromiseState& state() { return state_; }
    const SpawnTaskPromiseState& state() const { return state_; }


    Result& result() & {
        switch (result_.index()) {
            case 0:
                throw HuCoroGeneralErr("The spawn task has not finished, this should not happend");
            case 1:
                return std::get<1>(result_);
            case 2:
                std::rethrow_exception(std::get<2>(result_));
            default:
                __builtin_unreachable();
        }
    }

    Result&& result() && {
        switch (result_.index()) {
            case 0:
                throw HuCoroGeneralErr("The spawn task has not finished, this should not happend");
            case 1:
                return static_cast<Result&&>(std::get<1>(result_));
            case 2:
                std::rethrow_exception(std::get<2>(result_));
            default:
                __builtin_unreachable();
        }
    }


private:
    SpawnTaskPromiseState state_;
    std::variant<std::monostate, Result, std::exception_ptr> result_ = nullptr;
};

template<>
class SpawnTaskPromise<void> {
    static_assert(std::atomic<State>::is_always_lock_free);

public:
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::pair<JoinHandle<void>, SpawnTask> get_return_object();
    void unhandled_exception() { result_ = std::current_exception(); }
    void return_void() {}
    detail::SpawnTaskFinalAwaiter<void> final_suspend() noexcept {
        return {std::coroutine_handle<SpawnTaskPromise<void>>::from_promise(*this)};
    }

    void result() {
        if (result_) { std::rethrow_exception(result_); }
    }

    SpawnTaskPromiseState& state() { return state_; }
    const SpawnTaskPromiseState& state() const { return state_; }

private:
    SpawnTaskPromiseState state_;
    std::exception_ptr result_ = nullptr;
};


// SpawnTask must be the first resumer of SpawnTaskPromise
class SpawnTask {
public:
    SpawnTask(SpawnTaskPromiseState& state, std::coroutine_handle<> handle) noexcept : state_(state), handle_(handle) {
        state.incr_ref();
    }
    SpawnTask(const SpawnTask&) = delete;
    SpawnTask& operator=(const SpawnTask&) = delete;
    SpawnTask(SpawnTask&& other) : state_(other.state_), handle_(std::exchange(other.handle_, nullptr)) {}
    ~SpawnTask() {
        if (handle_ && state_.decr_ref() == 0 && state_.state() == State::INIT) { handle_.destroy(); }
    }
    void resume() {
        State state = State::INIT;
        if (!state_.state_.compare_exchange_strong(state, State::IN_PROGRESS, std::memory_order_acq_rel)) {
            assert(state == State::WAITING_TO_RESUME);
        }
        handle_.resume();
    }

private:
    SpawnTaskPromiseState& state_;
    std::coroutine_handle<> handle_;
};

template<typename Result>
class JoinHandleBase {
public:
    using coroutine_handle_t = std::coroutine_handle<SpawnTaskPromise<Result>>;
    JoinHandleBase(coroutine_handle_t handle) : coroutine_(handle) { handle.promise().state().incr_ref(); }
    JoinHandleBase(const JoinHandleBase& handle) = delete;
    JoinHandleBase& operator=(const JoinHandleBase& handle) = delete;

    JoinHandleBase(JoinHandleBase&& other) : coroutine_(std::exchange(other.coroutine_, nullptr)) {}

    ~JoinHandleBase() {
        if (!coroutine_) { return; }
        SpawnTaskPromiseState& state = coroutine_.promise().state();
        if (state.decr_ref() == 0 && state.state() == State::INIT) { coroutine_.destroy(); }
    }
    bool await_ready() noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> awaiting_coroutine) {
        auto& state = coroutine_.promise().state();

        state.set_awaiting_coroutine(awaiting_coroutine);
        State prev_state = state.state_.exchange(State::WAITING_TO_RESUME, std::memory_order_acq_rel);
        return prev_state != State::FINISH;
    }

protected:
    coroutine_handle_t coroutine_;
};

/// When call `join_handle.result()`, it may throw exception
/// if the corresponding coroutine throws exception internally.
template<typename Result>
class JoinHandle : public JoinHandleBase<Result> {
public:
    using JoinHandleBase<Result>::JoinHandleBase;
    Result& await_resume() & { return this->coroutine_.promise().result(); }

    Result&& await_resume() && { return std::move(this->coroutine_.promise()).result(); }
};

template<>
class JoinHandle<void> : public JoinHandleBase<void> {
public:
    using JoinHandleBase<void>::JoinHandleBase;
    void await_resume() { this->coroutine_.promise().result(); }
};

template<typename Result>
JoinHandle(std::coroutine_handle<SpawnTaskPromise<Result>>) -> JoinHandle<Result>;

template<typename Result>
std::pair<JoinHandle<Result>, SpawnTask> SpawnTaskPromise<Result>::get_return_object() {
    using coroutine_handle_t = std::coroutine_handle<SpawnTaskPromise<Result>>;
    coroutine_handle_t handle = coroutine_handle_t::from_promise(*this);
    return std::make_pair(JoinHandle{handle},
                          SpawnTask{this->state_, std::coroutine_handle<>::from_address(handle.address())});
}


}// namespace hucoro

template<typename Result, typename... Args>
struct std::coroutine_traits<std::pair<hucoro::JoinHandle<Result>, hucoro::SpawnTask>, Args...> {
    using promise_type = hucoro::SpawnTaskPromise<Result>;
};


#endif// HUCORO_SPAWN_TASK_H
