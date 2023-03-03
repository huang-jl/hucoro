//
// Created by dreamHuang on 2023/3/1.
//

#include "single_thread_scheduler.h"

namespace hucoro {

void SingleThreadScheduler::schedule(SingleThreadScheduler::schedule_operation_t* task) { tasks_.push_back(task); }

thread_local SingleThreadScheduler* SingleThreadScheduler::CURRENT_SCHEDULER = nullptr;

}// namespace hucoro