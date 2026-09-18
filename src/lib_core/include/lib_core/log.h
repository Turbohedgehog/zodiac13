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

#include <utility>

#include <spdlog/spdlog.h>

namespace z13 {

// format_string_t (not a plain forwarded pack) keeps fmt's compile-time format checking.
template <typename... Args>
void log_info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::info(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log_warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::warn(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log_error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::error(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log_debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::debug(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log_trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::trace(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log_critical(spdlog::format_string_t<Args...> fmt, Args&&... args) {
  spdlog::critical(fmt, std::forward<Args>(args)...);
}

}  // namespace z13
