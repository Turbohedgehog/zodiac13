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

#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace z13 {

struct ThreadPoolState;

// The process's worker threads, shared by every plugin: Core makes one and gives each world it
// as a singleton (docs/station-systems-plan.md, f/thread-pool). Not flecs' worker threads, which
// run systems. With one thread there are no workers: work runs on the caller, as a
// single-threaded check of determinism wants.
class ThreadPool {
 public:
  // Rebuilt, never saved: the shared_ptr pImpl exemption of CLAUDE.md.
  using Singleton = void;

  // Every loop of at least two indices goes to the pool unless `min_parallel_count` says more.
  static constexpr size_t kMinParallelCount = 2;

  ThreadPool();
  explicit ThreadPool(size_t threads, size_t min_parallel_count = kMinParallelCount);

  size_t Threads() const;

  // Runs body(index) once for every index in [0, count) and returns when all have run; on the
  // caller when count is below the minimum.
  // A body may call it again: the nested call works on the calling worker instead of blocking it.
  void ForEachIndex(size_t count, const std::function<void(size_t)>& body) const;

  bool operator==(const ThreadPool&) const = default;

 private:
  std::shared_ptr<ThreadPoolState> state_;
  size_t min_parallel_count_ {kMinParallelCount};
};

}  // namespace z13
