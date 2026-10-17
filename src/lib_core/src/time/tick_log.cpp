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

#include <lib_core/time/tick_log.h>

#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>

#include <lib_core/time/simulation_clock.h>

namespace z13::flecs_tools {

namespace {

constexpr char kTickFlag = '*';
// spdlog's default pattern without the logger name, plus the tick.
constexpr std::string_view kTickLogPattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [tick %*] %v";

}  // namespace

TickLogFlag::TickLogFlag(std::shared_ptr<const std::atomic<uint64_t>> tick) : tick_(std::move(tick)) {}

void TickLogFlag::format(const spdlog::details::log_msg&, const std::tm&, spdlog::memory_buf_t& dest) {
  std::format_to(std::back_inserter(dest), "{}", tick_->load(std::memory_order_relaxed));
}

std::unique_ptr<spdlog::custom_flag_formatter> TickLogFlag::clone() const {
  return std::make_unique<TickLogFlag>(tick_);
}

void ShowTickInLog(flecs::world& world) {
  auto tick = std::make_shared<std::atomic<uint64_t>>(world.get<SimulationClock>().tick);
  // PreFrame runs after the frame's increment, and in a frozen world too.
  world.system<const SimulationClock>("TickLog::Track")
      .kind(flecs::PreFrame)
      .each([tick](const SimulationClock& clock) { tick->store(clock.tick, std::memory_order_relaxed); });

  auto formatter = std::make_unique<spdlog::pattern_formatter>();
  formatter->add_flag<TickLogFlag>(kTickFlag, tick).set_pattern(std::string(kTickLogPattern));
  spdlog::set_formatter(std::move(formatter));
}

}  // namespace z13::flecs_tools
