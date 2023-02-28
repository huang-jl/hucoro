//
// Created by dreamHuang on 2023/2/25.
//

#include "catch2/catch_test_macros.hpp"
#include "counter.h"
#include "entry.h"
#include "task.h"
#include <iostream>

using hucoro::Task;
using hucoro::test::Counter;

Task<int> simple_coroutine(Counter c) { co_return 10; }

TEST_CASE("Simple Task Test without running", "[Task]") {
    Counter c;
    {
        REQUIRE(Counter::alive_num() == 1);
        auto task = simple_coroutine(c);
        REQUIRE(Counter::alive_num() == 2);
    }
    REQUIRE(Counter::alive_num() == 1);
    REQUIRE(Counter::total_copy_num() == 1);
    REQUIRE(Counter::total_move_num() == 1);
}

TEST_CASE("Simple Task Test", "[Task]") {
    {
        auto entry = hucoro::entry([]() -> Task<Counter> { co_return Counter{}; });
        // we have created the awaitable but has not co_awaited it
        REQUIRE(Counter::alive_num() == 0);
        // when `.run()`, it is supposed to finish before `.run()` returned.
        // so there is only a Counter object in the TaskPromise.
        entry.run();
        REQUIRE(Counter::alive_num() == 1);
    }

    REQUIRE(Counter::alive_num() == 0);
    auto&& res = hucoro::entry([]() -> Task<Counter> { co_return Counter{}; }).run();
    REQUIRE(Counter::alive_num() == 0); // this means the res above is a dangling reference
}
