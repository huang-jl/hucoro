//
// Created by dreamHuang on 2023/2/25.
//

#include "counter.h"

namespace hucoro {
namespace test {
    Counter::Counter() noexcept { default_ctor_num += 1; }
    Counter::Counter(const Counter&) noexcept { copy_ctor_num += 1; }
    Counter::Counter(Counter&&) noexcept { move_ctor_num += 1; }
    Counter::~Counter() noexcept { dtor_num += 1; }

    std::size_t Counter::default_ctor_num = 0;
    std::size_t Counter::copy_ctor_num = 0;
    std::size_t Counter::move_ctor_num = 0;
    std::size_t Counter::dtor_num = 0;
}// namespace test
}// namespace hucoro
