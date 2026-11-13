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

#include <memory>
#include <mutex>

#include <spdlog/sinks/base_sink.h>

#include <lib_core/utils/console_log.h>

namespace z13 {

// Hands spdlog's messages, without its time and level prefix, to a ConsoleLog.
class ConsoleLogSink : public spdlog::sinks::base_sink<std::mutex> {
 public:
  explicit ConsoleLogSink(ConsoleLog log);

 protected:
  void sink_it_(const spdlog::details::log_msg& message) override;
  void flush_() override {}

 private:
  ConsoleLog log_;
};

}  // namespace z13
