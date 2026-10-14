/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <lib_core/utils/thread_pool.h>

#include <taskflow/algorithm/for_each.hpp>

#include "thread_pool_state.h"

namespace z13 {

namespace {

// Taskflow's id of a thread that isn't one of the executor's workers.
constexpr int kNotAWorker = -1;

}  // namespace

ThreadPool::ThreadPool() = default;

ThreadPool::ThreadPool(size_t threads) {
  if (threads > 1) {
    state_ = std::make_shared<ThreadPoolState>(threads);
  }
}

size_t ThreadPool::Threads() const {
  return state_ ? state_->executor.num_workers() : 1;
}

void ThreadPool::ForEachIndex(size_t count, const std::function<void(size_t)>& body) const {
  if (!state_ || count < 2) {
    for (size_t index = 0; index < count; ++index) {
      body(index);
    }
    return;
  }
  tf::Taskflow flow;
  flow.for_each_index(size_t {0}, count, size_t {1}, body);
  tf::Executor& executor = state_->executor;
  if (executor.this_worker_id() == kNotAWorker) {
    executor.run(flow).wait();
  } else {
    executor.corun(flow);
  }
}

}  // namespace z13
