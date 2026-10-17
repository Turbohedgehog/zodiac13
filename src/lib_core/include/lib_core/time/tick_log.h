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

#include <atomic>
#include <cstdint>
#include <memory>

#include <flecs.h>
#include <spdlog/pattern_formatter.h>

namespace z13::flecs_tools {

// Atomic: pool threads log too.
class TickLogFlag : public spdlog::custom_flag_formatter {
 public:
  explicit TickLogFlag(std::shared_ptr<const std::atomic<uint64_t>> tick);

  void format(const spdlog::details::log_msg& message, const std::tm& time, spdlog::memory_buf_t& dest) override;
  std::unique_ptr<spdlog::custom_flag_formatter> clone() const override;

 private:
  std::shared_ptr<const std::atomic<uint64_t>> tick_;
};

// Call once the world is created.
void ShowTickInLog(flecs::world& world);

}  // namespace z13::flecs_tools
