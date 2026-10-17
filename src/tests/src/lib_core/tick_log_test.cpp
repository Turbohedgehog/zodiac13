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

#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <string>

#include <spdlog/pattern_formatter.h>

#include <lib_core/time/tick_log.h>

namespace z13 {
namespace {

namespace ft = z13::flecs_tools;

constexpr char kFlag = '*';
constexpr uint64_t kFirstTick = 41;

std::string Format(spdlog::pattern_formatter& formatter) {
  const spdlog::details::log_msg message("", spdlog::level::info, "hello");
  spdlog::memory_buf_t out;
  formatter.format(message, out);
  return {out.data(), out.size()};
}

TEST(TickLogTest, ALogLineShowsTheTickAtTheTimeItIsWritten) {
  auto tick = std::make_shared<std::atomic<uint64_t>>(kFirstTick);
  spdlog::pattern_formatter formatter;
  formatter.add_flag<ft::TickLogFlag>(kFlag, tick).set_pattern("[tick %*] %v");

  const auto expected = [](uint64_t at) {
    return std::format("[tick {}] hello{}", at, spdlog::details::os::default_eol);
  };
  EXPECT_EQ(Format(formatter), expected(kFirstTick));
  tick->store(kFirstTick + 1);
  EXPECT_EQ(Format(formatter), expected(kFirstTick + 1));
}

}  // namespace
}  // namespace z13
