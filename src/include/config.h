//
// Created by dreamHuang on 2023/2/25.
//

#ifndef HUCORO_CONFIG_H
#define HUCORO_CONFIG_H

#if defined(__clang__)
#    define HUCORO_CLANG_COMPILER
#elif defined(__GNUG__)
#    define HUCORO_GCC_COMPILER
#else
#    error "unsupport compiler: for now only support clang++ and g++"
#endif


#if defined(HUCORO_CLANG_COMPILER)
#    if __has_include(<coroutine>)
#        pragma message("use clang >= 13 to compile: with <coroutine>")
#        include <coroutine>
}// namespace hucoro
#    elif __has_include(<experimental/coroutine>)
#        pragma message("use clang >= 13 to compile: with <experimental/coroutine>")
#        include <experimental/coroutine>
namespace std {
template<typename T = void>
using coroutine_handle = std::experimental::coroutine_handle<T>;
using suspend_always = std::experimental::suspend_always;
using suspend_never = std::experimental::suspend_never;
using std::experimental::noop_coroutine;
}// namespace std
#    else
static_assert(false, "could not found coroutine header in clang");
#    endif
#else
// gcc
#    if __has_include(<coroutine>)
#        pragma message("use g++ to compile: with <coroutine>")
#        include <coroutine>
#    else
#        error "unsupport compiler: for now only support clang++ and g++"
#    endif

#endif

#endif//HUCORO_CONFIG_H
