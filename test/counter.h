//
// Created by dreamHuang on 2023/2/25.
//

#ifndef HUCORO_COUNTER_H
#define HUCORO_COUNTER_H

#include <cstddef>

namespace hucoro {
namespace test {
    struct Counter {
        Counter() noexcept;
        Counter(const Counter&) noexcept;
        Counter(Counter&&) noexcept;
        ~Counter() noexcept;

        Counter& operator=(const Counter&) = default;
        Counter& operator=(Counter&&) = default;

        static inline std::size_t alive_num() { return default_ctor_num + copy_ctor_num + move_ctor_num - dtor_num; }
        static inline std::size_t total_copy_num()  { return copy_ctor_num; }
        static inline std::size_t total_move_num() { return move_ctor_num; }

    private:
        // all initialize to zero
        static std::size_t default_ctor_num;
        static std::size_t copy_ctor_num;
        static std::size_t move_ctor_num;
        static std::size_t dtor_num;
    };
}// namespace test
}// namespace hucoro

#endif//HUCORO_COUNTER_H
