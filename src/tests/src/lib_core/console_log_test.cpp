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

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/logger.h>

#include <lib_core/utils/console_log.h>

namespace z13 {
namespace {

constexpr int kThreads = 4;
constexpr int kLinesPerThread = 50;
constexpr std::size_t kMoreThanTheBuffer = 1500;

TEST(ConsoleLogTest, KeepsTheLastLinesAndEveryLineChangesTheRevision) {
  const ConsoleLog log;
  for (std::size_t i = 0; i < kMoreThanTheBuffer; ++i) {
    log.Add(spdlog::level::info, std::to_string(i));
  }

  const std::vector<LogLine> lines = log.Lines();
  EXPECT_EQ(log.Revision(), kMoreThanTheBuffer);
  EXPECT_LT(lines.size(), kMoreThanTheBuffer);
  EXPECT_EQ(lines.back().text, std::to_string(kMoreThanTheBuffer - 1));
}

TEST(ConsoleLogTest, ClearEmptiesTheLinesAndChangesTheRevision) {
  const ConsoleLog log;
  log.Add(spdlog::level::info, "one");
  log.Clear();

  EXPECT_TRUE(log.Lines().empty());
  EXPECT_EQ(log.Revision(), 2U);
}

TEST(ConsoleLogTest, CopiesShareOneBuffer) {
  const ConsoleLog log;
  const ConsoleLog copy = log;
  copy.Add(spdlog::level::warn, "from the copy");

  ASSERT_EQ(log.Lines().size(), 1U);
  EXPECT_EQ(log.Lines().front().level, spdlog::level::warn);
}

TEST(ConsoleLogTest, TheSinkTakesTheMessageWithoutItsPrefixAndKeepsTheLevel) {
  const ConsoleLog log;
  spdlog::logger logger("console_log_test", log.Sink());
  logger.set_pattern("[%l] %v");

  logger.error("boom {}", 7);

  const std::vector<LogLine> lines = log.Lines();
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_EQ(lines.front().text, "boom 7");
  EXPECT_EQ(lines.front().level, spdlog::level::err);
}

TEST(ConsoleLogTest, ThreadsAddWithoutLosingALine) {
  const ConsoleLog log;
  std::vector<std::thread> threads;
  for (int thread = 0; thread < kThreads; ++thread) {
    threads.emplace_back([&log] {
      for (int i = 0; i < kLinesPerThread; ++i) {
        log.Add(spdlog::level::info, "x");
      }
    });
  }
  std::ranges::for_each(threads, [](std::thread& thread) { thread.join(); });

  EXPECT_EQ(log.Revision(), static_cast<std::uint64_t>(kThreads * kLinesPerThread));
}

}  // namespace
}  // namespace z13
