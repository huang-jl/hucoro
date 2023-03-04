//
// Created by dreamHuang on 2023/3/3.
//

#include "spawn_task.h"
#include "config.h"
#include <utility>

namespace hucoro {
std::pair<JoinHandle<void>, SpawnTask> SpawnTaskPromise<void>::get_return_object() {
    using coroutine_handle_t = std::coroutine_handle<SpawnTaskPromise<void>>;
    coroutine_handle_t handle = coroutine_handle_t::from_promise(*this);
    return std::make_pair(JoinHandle<void>{handle},
                          SpawnTask{this->state_, std::coroutine_handle<>::from_address(handle.address())});
}
}// namespace hucoro
