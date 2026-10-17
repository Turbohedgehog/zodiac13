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

#include <z13_launcher/log_file.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <z13/constants/constants.h>

namespace z13 {

namespace {

constexpr std::string_view kLogExtension = ".log";

std::string LogFilePrefix() {
  return std::format("{}-", constants::kGameName);
}

bool IsRunLog(const std::filesystem::directory_entry& entry) {
  const std::filesystem::path& path = entry.path();
  return entry.is_regular_file() && path.extension() == kLogExtension &&
         path.filename().string().starts_with(LogFilePrefix());
}

std::filesystem::path NewLogPath(const std::filesystem::path& directory) {
  const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
  return directory / std::format("{}{:%Y%m%d-%H%M%S}{}", LogFilePrefix(), now, kLogExtension);
}

}  // namespace

// Names start with the UTC start time, so name order is age order.
Status RemoveOldLogs(const std::filesystem::path& directory) {
  std::error_code error;
  if (!std::filesystem::exists(directory, error)) {
    return {};
  }
  std::vector<std::filesystem::path> logs;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
    if (IsRunLog(entry)) {
      logs.push_back(entry.path());
    }
  }
  if (error) {
    return std::unexpected(std::format("cannot list '{}': {}", directory.string(), error.message()));
  }
  std::ranges::sort(logs);
  const std::size_t excess = logs.size() >= kKeptLogFiles ? logs.size() - kKeptLogFiles + 1 : 0;
  for (const auto& old_log : logs | std::views::take(excess)) {
    if (!std::filesystem::remove(old_log, error)) {
      return std::unexpected(std::format("cannot remove '{}': {}", old_log.string(), error.message()));
    }
  }
  return {};
}

std::expected<std::filesystem::path, std::string> AddLogFile(const std::filesystem::path& directory) {
  const std::filesystem::path path = NewLogPath(directory);
  // spdlog reports a file it can't open only by throwing; it creates missing directories itself.
  try {
    spdlog::default_logger()->sinks().push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string()));
  } catch (const spdlog::spdlog_ex& ex) {
    return std::unexpected(std::format("cannot open the log file: {}", ex.what()));
  }
  return path;
}

}  // namespace z13
