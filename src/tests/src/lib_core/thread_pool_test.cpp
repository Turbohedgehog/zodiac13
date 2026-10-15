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

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include <lib_core/settings/core_settings.h>
#include <lib_core/utils/thread_pool.h>
#include <lib_core/world/core.h>

namespace z13 {
namespace {

constexpr std::array kThreadCounts = {size_t {1}, size_t {2}, size_t {4}};
constexpr std::array kCounts = {size_t {0}, size_t {1}, size_t {7}, size_t {1000}};
constexpr size_t kOuterCount = 8;
constexpr size_t kInnerCount = 100;
constexpr uint32_t kCoreThreads = 3;

TEST(ThreadPoolTest, EveryIndexRunsExactlyOnce) {
  for (const size_t threads : kThreadCounts) {
    const ThreadPool pool(threads);
    for (const size_t count : kCounts) {
      std::vector<std::atomic<int>> runs(count);
      pool.ForEachIndex(count, [&runs](size_t index) { runs[index].fetch_add(1); });
      for (size_t index = 0; index < count; ++index) {
        EXPECT_EQ(runs[index].load(), 1) << threads << " threads, index " << index << " of " << count;
      }
    }
  }
}

TEST(ThreadPoolTest, OneThreadRunsOnTheCaller) {
  const ThreadPool pool(1);
  EXPECT_EQ(pool.Threads(), 1U);
  std::vector<std::thread::id> runners(kCounts.back());
  pool.ForEachIndex(runners.size(), [&runners](size_t index) { runners[index] = std::this_thread::get_id(); });
  for (const std::thread::id runner : runners) {
    EXPECT_EQ(runner, std::this_thread::get_id());
  }
}

TEST(ThreadPoolTest, ALoopBelowTheMinimumRunsOnTheCaller) {
  constexpr size_t kMinParallelCount = 10;
  const ThreadPool pool(kThreadCounts.back(), kMinParallelCount);
  std::vector<std::thread::id> runners(kMinParallelCount - 1);
  pool.ForEachIndex(runners.size(), [&runners](size_t index) { runners[index] = std::this_thread::get_id(); });
  for (const std::thread::id runner : runners) {
    EXPECT_EQ(runner, std::this_thread::get_id());
  }
}

TEST(ThreadPoolTest, ANestedCallReturns) {
  const ThreadPool pool(kThreadCounts.back());
  std::atomic<size_t> runs {};
  pool.ForEachIndex(kOuterCount, [&](size_t) {
    pool.ForEachIndex(kInnerCount, [&runs](size_t) { runs.fetch_add(1); });
  });
  EXPECT_EQ(runs.load(), kOuterCount * kInnerCount);
}

TEST(ThreadPoolTest, EveryWorldOfACoreSharesItsPool) {
  std::string program = "z13_test";
  std::array argv = {program.data()};
  Core core(static_cast<int>(argv.size()), argv.data());
  CoreSettings settings = core.GetConfig().GetCoreSettings();
  settings.threads = kCoreThreads;
  core.GetConfig().SetCoreSettings(settings);

  const auto first = core.CreateWorld();
  const auto second = core.CreateWorld();
  ASSERT_TRUE(first.has_value()) << first.error();
  ASSERT_TRUE(second.has_value()) << second.error();
  const ThreadPool& pool = first->get().get<ThreadPool>();
  EXPECT_EQ(pool.Threads(), kCoreThreads);
  EXPECT_EQ(pool, second->get().get<ThreadPool>());
}

}  // namespace
}  // namespace z13
