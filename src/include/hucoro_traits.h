//
// Created by dreamHuang on 2023/2/26.
//

#ifndef HUCORO_HUCORO_TRAITS_H
#define HUCORO_HUCORO_TRAITS_H

#include "config.h"
#include <type_traits>
#include <utility>

namespace hucoro {
namespace {
    using handle_t = std::coroutine_handle<>;
}// namespace

template<typename T, typename U = void>
struct is_awaiter : std::false_type {};

template<typename T>
struct is_awaiter<T, std::enable_if_t<
                             std::is_same_v<decltype(std::declval<T>().await_resume(), std::declval<T>().await_ready()), bool> &&
                                     std::disjunction_v<
                                             std::is_void<decltype(std::declval<T>().await_suspend(std::declval<handle_t>()))>,
                                             std::is_same<decltype(std::declval<T>().await_suspend(std::declval<handle_t>())), bool>,
                                             std::is_same<decltype(std::declval<T>().await_suspend(std::declval<handle_t>())), handle_t>>,
                             void>> : std::true_type {
    using await_return_type = decltype(std::declval<T>().await_resume());
};

template<typename T, typename U = void>
inline constexpr bool is_awaiter_v = is_awaiter<T, U>::value;


template<typename T, typename = void>
struct awaitable_traits {};

template<typename T>
struct awaitable_traits<T, std::enable_if_t<is_awaiter_v<decltype(std::declval<T>().operator co_await())>>> {
    using awaiter_type = decltype(std::declval<T>().operator co_await());
    using await_return_type = typename is_awaiter<awaiter_type>::await_return_type;
};

template<typename T>
struct awaitable_traits<T, std::enable_if_t<is_awaiter_v<decltype(operator co_await(std::declval<T>()))>>> {
    using awaiter_type = decltype(operator co_await(std::declval<T>()));
    using await_return_type = typename is_awaiter<awaiter_type>::await_return_type;
};

template<typename T>
struct awaitable_traits<T, std::enable_if_t<
                                   !is_awaiter_v<decltype(std::declval<T>().operator co_await())> &&
                                   !is_awaiter_v<decltype(operator co_await(std::declval<T>()))> &&
                                   is_awaiter_v<T>>> {
    using awaiter_type = T;
    using await_return_type = typename is_awaiter<T>::await_return_type;
};


template<typename T, typename = void>
struct is_awaitable : std::false_type {};

template<typename T>
struct is_awaitable<T, std::void_t<typename awaitable_traits<T>::awaiter_type>> : std::true_type {};

template<typename T>
inline constexpr bool is_awaitable_v = is_awaitable<T, void>::value;

}// namespace hucoro


#endif//HUCORO_HUCORO_TRAITS_H
