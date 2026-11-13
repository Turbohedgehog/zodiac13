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


#include <lib_core/utils/console_log.h>

#include <utility>

#include "console_log_sink.h"
#include "console_log_state.h"

namespace z13 {

ConsoleLog::ConsoleLog() : state_(std::make_shared<State>()) {}

std::shared_ptr<spdlog::sinks::sink> ConsoleLog::Sink() const {
  return std::make_shared<ConsoleLogSink>(*this);
}

void ConsoleLog::Add(spdlog::level::level_enum level, std::string text) const {
  const std::scoped_lock lock(state_->mutex);
  state_->lines.push_back({.level = level, .text = std::move(text)});
  if (state_->lines.size() > kConsoleLogCapacity) {
    state_->lines.pop_front();
  }
  ++state_->revision;
}

std::vector<LogLine> ConsoleLog::Lines() const {
  const std::scoped_lock lock(state_->mutex);
  return {state_->lines.begin(), state_->lines.end()};
}

std::uint64_t ConsoleLog::Revision() const {
  const std::scoped_lock lock(state_->mutex);
  return state_->revision;
}

void ConsoleLog::Clear() const {
  const std::scoped_lock lock(state_->mutex);
  state_->lines.clear();
  ++state_->revision;
}

}  // namespace z13
