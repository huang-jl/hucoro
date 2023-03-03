//
// Created by dreamHuang on 2023/2/26.
//


#include "catch2/catch_test_macros.hpp"
#include "counter.h"
#include "hucoro_traits.h"
#include "task.h"


using hucoro::awaitable_traits;
using hucoro::is_awaitable;
using hucoro::is_awaiter;
using hucoro::Task;

struct Foo {};

TEST_CASE("awaiter", "[Traits]") {
    REQUIRE(is_awaiter<decltype(std::declval<Task<int>>().operator co_await())>::value == true);
    REQUIRE(is_awaiter<Task<int&>>::value == false);
    REQUIRE(is_awaiter<Foo>::value == false);
}

TEST_CASE("awaitable", "[Traits]") {
    REQUIRE(is_awaitable<Task<int>>::value == true);
    REQUIRE(is_awaitable<Foo>::value == false);
    REQUIRE(std::is_same_v<awaitable_traits<Task<int>>::await_return_type, int&&>);
}
