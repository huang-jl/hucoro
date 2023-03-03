//
// Created by dreamHuang on 2023/2/25.
//

#include "catch2/catch_test_macros.hpp"
#include "counter.h"
#include "single_thread_scheduler.h"
#include "task.h"
#include <iostream>

using hucoro::Task;
using hucoro::test::Counter;

Task<int> simple_coroutine(Counter c) { co_return 10; }

TEST_CASE("Simple Task Test without running", "[Task]") {
    bool compiler_opt = false;
    {
        auto task = simple_coroutine(Counter{});
        // The compiler may opt out the Counter c, so the alive_num can be 0
        REQUIRE(Counter::alive_num() <= 1);
        compiler_opt = (Counter::alive_num() == 0);
    }
    REQUIRE(Counter::alive_num()== 0);
}

TEST_CASE("Simple Task Test", "[Task]") {
    hucoro::SingleThreadScheduler scheduler;
    auto before_default_ctor_num = Counter::total_default_ctor_num();
    auto before_total_move_num = Counter::total_move_num();
    {
        REQUIRE(Counter::alive_num() == 0);
        // there is a move ctor to create the counter
        auto counter = scheduler.block_on([]() -> Task<Counter> { co_return Counter{}; });
        REQUIRE(Counter::alive_num() == 1);
        // ctor happened in `Counter{}`
        REQUIRE(Counter::total_default_ctor_num() - before_default_ctor_num == 1);
        // move happened first from co_return into TaskPromise
        // move happened second from block_on into returned `counter`
        // but without compiler's CopyElision there may be 3 move ctor
        auto move_num = Counter::total_move_num() - before_total_move_num;
        REQUIRE(move_num <= 3);
        REQUIRE(move_num >= 2);
    }

    REQUIRE(Counter::alive_num() == 0);
    auto&& res = scheduler.block_on([]() -> Task<Counter> { co_return Counter{}; });
    // this means the res above is NOT a dangling reference
    REQUIRE(Counter::alive_num() == 1);
}

TEST_CASE("Spawn Task", "[Task]") {
    hucoro::SingleThreadScheduler scheduler;
    auto val = scheduler.block_on([]() -> Task<int> {
        auto val = co_await hucoro::SingleThreadScheduler::spawn([]() -> Task<int> { co_return 100; });
        co_return val;
    });
    REQUIRE(val == 100);
}
