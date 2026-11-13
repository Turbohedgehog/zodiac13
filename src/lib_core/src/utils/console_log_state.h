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
#include <cstdint>
#include <deque>
#include <mutex>

#include <lib_core/utils/console_log.h>

namespace z13 {

// More than a screen of lines can show, few enough to copy every frame.
inline constexpr std::size_t kConsoleLogCapacity = 1000;

struct ConsoleLog::State {
  mutable std::mutex mutex;
  std::deque<LogLine> lines;
  std::uint64_t revision {};
};

}  // namespace z13
