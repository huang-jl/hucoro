//
// Created by dreamHuang on 2023/2/26.
//

#ifndef HUCORO_ENTRY_H
#define HUCORO_ENTRY_H

#include "config.h"
#include "exception.h"
#include "hucoro_traits.h"
#include <atomic>
#include <condition_variable>
#include <variant>

namespace hucoro {
namespace detail {


    class EntryTaskSyncPrimitive {
    public:
        EntryTaskSyncPrimitive() : status_(false) {}
        void set() noexcept {
            if (!status_.exchange(true, std::memory_order_acq_rel)) { cv_.notify_one(); }
        }

        void wait() {
            while (!status_.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lock(lock_);
                cv_.wait(lock);
            }
        }

    private:
        std::condition_variable cv_;
        std::atomic<bool> status_;
        std::mutex lock_;
    };

    /// This EntryPromise is almose the same as TaskPromise, except:
    /// 1. It will not need to resume the awaiting coroutine, since there will not be any awaiting coroutine
    /// (so it does need a variable to save it).
    /// 2. It will need to sync with the main / the caller of `Entry::run()` in final_suspend()
    template<typename RESULT>
    class EntryPromise;

    template<typename RESULT>
    struct EntryTask {
        using promise_type = EntryPromise<RESULT>;
        using coroutine_handle_t = typename std::coroutine_handle<promise_type>;

        explicit EntryTask(coroutine_handle_t handle_) noexcept : coroutine_handle_(handle_) {}

        EntryTask(const EntryTask&) = delete;
        EntryTask& operator=(const EntryTask&) = delete;

        EntryTask(EntryTask&& other) { coroutine_handle_ = std::exchange(other.coroutine_handle_, nullptr); }
        EntryTask& operator=(EntryTask&& other) {
            if (this != &other) {
                if (coroutine_handle_) { coroutine_handle_.destroy(); }
                coroutine_handle_ = std::exchange(other.coroutine_handle_, nullptr);
            }
            return *this;
        }

        ~EntryTask() {
            if (coroutine_handle_) { coroutine_handle_.destroy(); }
        }

        void resume(EntryTaskSyncPrimitive& sync_primitive) {
            coroutine_handle_.promise().set_sync_primitive(sync_primitive);
            coroutine_handle_.resume();
        }

        decltype(auto) result() & { return coroutine_handle_.promise().result(); }
        decltype(auto) result() && { return std::move(coroutine_handle_.promise()).result(); }

    private:
        coroutine_handle_t coroutine_handle_;
    };


    class EntryPromiseBase {
    public:
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept {
            // TODO Can we direct call sync_primitive here ?
            // Since the entry task has not finished / suspended yet.
            // But the awaitable has already been co_awaited and finished.
            assert(sync_primitive_);
            sync_primitive_->set();
            return {};
        }

        void set_sync_primitive(EntryTaskSyncPrimitive& sync_primitive) { sync_primitive_ = &sync_primitive; }

    private:
        /// This is a workaround, since for now compiler do not support peek argument in ctor of promise type
        EntryTaskSyncPrimitive* sync_primitive_ = nullptr;
    };

    template<typename RESULT>
    class EntryPromise : public EntryPromiseBase {
    public:
        auto get_return_object() { return EntryTask{std::coroutine_handle<EntryPromise<RESULT>>::from_promise(*this)}; }
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
        // Result (i.e. The return value of `co_await awaitable`) is supposed to store in the awaitable promise (e.g. TaskPromise).
        // It will not need to copy/move into here. We just use a pointer.
        std::variant<std::monostate, RESULT*, std::exception_ptr> result_;
    };


    template<>
    class EntryPromise<void> : public EntryPromiseBase {
    public:
        auto get_return_object() { return EntryTask{std::coroutine_handle<EntryPromise<void>>::from_promise(*this)}; }
        void return_void() noexcept {}
        void unhandled_exception() { exception_ptr_ = std::current_exception(); }

    private:
        std::exception_ptr exception_ptr_ = nullptr;
    };


    template<typename Awaitable, typename = std::enable_if<is_awaitable_v<Awaitable>>>
    class Entry {
        // NOTE that discard the reference
        using await_return_without_ref_t =
                typename std::remove_reference_t<typename awaitable_traits<Awaitable>::await_return_type>;
        using coroutine_handle_t = typename std::coroutine_handle<EntryPromise<await_return_without_ref_t>>;


    public:
        Entry(Awaitable&& awaitable) : awaitable_(std::forward<Awaitable>(awaitable)) {}

        decltype(auto) run() & {
            // define some sync primitive
            EntryTaskSyncPrimitive sync_primitive;
            auto wrapper = run_impl();
            wrapper.resume(sync_primitive);
            // await for sync primitive
            sync_primitive.wait();
            return wrapper.result();
        }

        /// If `.run()` with prvalue `Entry`, we should return a rvalue reference instead of lvalue reference.
        /// Since the awaitable will destruct with the `Entry`, the return value will not be valid after destroying.
        decltype(auto) run() && {
            // define some sync primitive
            EntryTaskSyncPrimitive sync_primitive;
            auto wrapper = run_impl();
            wrapper.resume(sync_primitive);
            // await for sync primitive
            sync_primitive.wait();
            return std::move(wrapper).result();
        }


    private:
        // We copy/move the awaitable here, so it is supposed to live as long as `Entry`
        Awaitable awaitable_;

        template<class U = await_return_without_ref_t>
        EntryTask<await_return_without_ref_t> run_impl(std::enable_if_t<std::is_same_v<U, void>, int> = 0) {
            co_await awaitable_;
        }

        template<class U = await_return_without_ref_t>
        EntryTask<await_return_without_ref_t> run_impl(std::enable_if_t<!std::is_same_v<U, void>, int> = 0) {
            co_return co_await awaitable_;
        }
    };

    template<typename Awaitable>
    Entry(Awaitable&&) -> Entry<Awaitable, void>;

}// namespace detail

/// FUNC here means a function, which will be the starter of the whole coroutine.
/// The returned type of FUNC should be an awaitable.
/// Example:
///
/// ```c++
/// #include "hucoro/entry.h"
///
/// Task<int> simple_async_task() {
///     co_return 2022310806;
/// }
///
/// int main() {
///     int val = hucoro::entry([]() {
///                   return simple_async_task();
///               }).run();
///     assert(val == 2022310806);
///     return 0;
/// }
///```
template<typename FUNC>
auto entry(FUNC&& starter, std::enable_if_t<is_awaitable_v<decltype(starter())>, int> = 0) {
    return detail::Entry(starter());
}

}// namespace hucoro


#endif//HUCORO_ENTRY_H
