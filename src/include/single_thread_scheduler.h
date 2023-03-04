//
// Created by dreamHuang on 2023/3/1.
//

#ifndef HUCORO_SINGLE_THREAD_SCHEDULER_H
#define HUCORO_SINGLE_THREAD_SCHEDULER_H

#include "config.h"
#include "exception.h"
#include "hucoro_traits.h"
#include "spawn_task.h"
#include "task.h"
#include <deque>
#include <mutex>
#include <type_traits>
#include <variant>

namespace hucoro {
namespace detail {}// namespace detail

// single thread scheduler
class SingleThreadScheduler {

public:
    void schedule(SpawnTask&& task);

    template<typename FUNC>
    auto block_on(FUNC func, std::enable_if_t<is_awaitable_v<decltype(func())>, int> = 0);

    /// The returned type of spawn is hucoro::SpawnTask
    template<typename FUNC>
    static auto spawn(FUNC&& func) {
        if (!CURRENT_SCHEDULER) {
            throw HuCoroGeneralErr("Try spawn out side the scope of scheduler, which "
                                   "is not supported for now");
        }
        auto [join_handle, spawn_task] = CURRENT_SCHEDULER->spawn_impl(std::forward<FUNC>(func));
        // should not use spawn_task after this
        CURRENT_SCHEDULER->schedule(std::move(spawn_task));
        return std::move(join_handle);
    }

    thread_local static SingleThreadScheduler* CURRENT_SCHEDULER;

private:
    template<typename FUNC>
    std::pair<JoinHandle<void>, SpawnTask>
    spawn_impl(FUNC func,
               std::enable_if_t<std::is_same_v<typename awaitable_traits<decltype(func())>::await_return_type, void>,
                                int> = 0) {
        co_await func();
    }

    template<typename FUNC>
    auto
    spawn_impl(FUNC func,
               std::enable_if_t<!std::is_same_v<typename awaitable_traits<decltype(func())>::await_return_type, void>,
                                int> = 0)
            -> std::pair<
                    JoinHandle<std::remove_reference_t<typename awaitable_traits<decltype(func())>::await_return_type>>,
                    SpawnTask> {

        co_return co_await func();
    }

    /* data member */
    std::deque<SpawnTask> tasks_;
};

// A special task and promise for block_on
namespace detail {
    /// This EntryPromise is almose the same as TaskPromise, except:
    /// 1. It will not need to resume the awaiting coroutine, since there will not
    /// be any awaiting coroutine (so it does need a variable to save it).
    /// 2. It will need to sync with the main / the caller of `Entry::run()` in
    /// final_suspend()
    template<typename RESULT>
    class BlockOnPromise;

    template<typename RESULT>
    struct BlockOnTask {
        using promise_type = BlockOnPromise<RESULT>;
        using coroutine_handle_t = typename std::coroutine_handle<promise_type>;

        explicit BlockOnTask(coroutine_handle_t handle_) noexcept : coroutine_handle_(handle_) {}
        BlockOnTask(BlockOnTask&& other) { coroutine_handle_ = std::exchange(other.coroutine_handle_, nullptr); }
        BlockOnTask(const BlockOnTask&) = delete;

        BlockOnTask& operator=(BlockOnTask&& other) {
            if (coroutine_handle_) { coroutine_handle_.destroy(); }
            coroutine_handle_ = std::exchange(other.coroutine_handle_, nullptr);
            return *this;
        }
        BlockOnTask& operator=(const BlockOnTask& other) = delete;
        ~BlockOnTask() {
            if (coroutine_handle_) { coroutine_handle_.destroy(); }
        }

        void resume() { coroutine_handle_.resume(); }

        inline bool done() { return coroutine_handle_.done(); }

        decltype(auto) result() & { return coroutine_handle_.promise().result(); }
        decltype(auto) result() && { return std::move(coroutine_handle_.promise()).result(); }

    private:
        coroutine_handle_t coroutine_handle_;
    };

    class BlockOnPromiseBase {
    public:
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
    };

    template<typename RESULT>
    class BlockOnPromise : public BlockOnPromiseBase {
    public:
        auto get_return_object() {
            return BlockOnTask{std::coroutine_handle<BlockOnPromise<RESULT>>::from_promise(*this)};
        }
        void return_value(RESULT& result) noexcept { result_ = std::addressof(result); }
        void unhandled_exception() { result_ = std::current_exception(); }

        RESULT& result() & {
            switch (result_.index()) {
                case 0:
                    throw HuCoroGeneralErr("EntryTask has not get return value");
                case 1:
                    return *std::get<1>(result_);
                case 2:
                    std::rethrow_exception(std::get<2>(result_));
                default:
                    __builtin_unreachable();
            }
        }

        RESULT&& result() && {
            switch (result_.index()) {
                case 0:
                    throw HuCoroGeneralErr("EntryTask has not get return value");
                case 1:
                    return static_cast<RESULT&&>(*std::get<1>(result_));
                case 2:
                    std::rethrow_exception(std::get<2>(result_));
                default:
                    __builtin_unreachable();
            }
        }

    private:
        // Result (i.e. The return value of `co_await awaitable`) is supposed to store
        // in the awaitable promise (e.g. TaskPromise). It will not need to copy/move
        // into here. We just use a pointer.
        std::variant<std::monostate, RESULT*, std::exception_ptr> result_;
    };

    template<>
    class BlockOnPromise<void> : public BlockOnPromiseBase {
    public:
        BlockOnTask<void> get_return_object() {
            return BlockOnTask{std::coroutine_handle<BlockOnPromise<void>>::from_promise(*this)};
        }
        void return_void() noexcept {}
        void unhandled_exception() { exception_ptr_ = std::current_exception(); }

    private:
        std::exception_ptr exception_ptr_ = nullptr;
    };

    namespace {
        // The lifetime of awaitable end after the return value (i.e. `BlockOnTask`) has
        // out of scope, since the coroutine frame will be destroyed with RAII:
        // 1. It is safe to store a pointer to return value in BlockOnTaskPromise
        // (awaitable still alive)
        // 2. In `block_on` we cannot return a reference (no matter rvalue or lvalue),
        // since the block_on_task will be destroyed in the `block_on`
        template<typename Awaitable, typename await_return_without_ref = std::remove_reference_t<
                                             typename awaitable_traits<Awaitable>::await_return_type>>
        BlockOnTask<void> run_impl(Awaitable awaitable,
                                   std::enable_if_t<std::is_same_v<await_return_without_ref, void>, int> = 0) {
            co_await awaitable;
        }

        template<typename Awaitable, typename await_return_without_ref = std::remove_reference_t<
                                             typename awaitable_traits<Awaitable>::await_return_type>>
        BlockOnTask<await_return_without_ref>
        run_impl(Awaitable awaitable, std::enable_if_t<!std::is_same_v<await_return_without_ref, void>, int> = 0) {
            co_return co_await awaitable;
        }
    }// namespace

}// namespace detail

template<typename FUNC>
auto SingleThreadScheduler::block_on(FUNC func, std::enable_if_t<is_awaitable_v<decltype(func())>, int>) {
    // set the thread local variable
    CURRENT_SCHEDULER = this;

    auto block_on_task = detail::run_impl(func());
    block_on_task.resume();
    while (1) {
        // first check block_on_tasks
        if (block_on_task.done()) { goto FINISH_BLOCK_ON; }

        // pop task from task queue to execute
        for (int i = 0; i < 10; ++i) {
            if (tasks_.empty()) { continue; }
            SpawnTask task = std::move(tasks_.front());
            tasks_.pop_front();
            task.resume();
        }
    }

FINISH_BLOCK_ON:
    assert(block_on_task.done());
    CURRENT_SCHEDULER = nullptr;
    // a move happened since the return type is `auto` (not `decltype(auto)`),
    // which will be decayed to non-reference type
    return std::move(block_on_task).result();
}

}// namespace hucoro
#endif// HUCORO_SINGLE_THREAD_SCHEDULER_H
