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
#include <expected>
#include <filesystem>
#include <string>

#include <lib_core/utils/status.h>

namespace z13 {

// Run logs kept in the directory, this run's included.
constexpr std::size_t kKeptLogFiles = 10;

// Deletes the oldest run logs in `directory`, leaving room for one more.
Status RemoveOldLogs(const std::filesystem::path& directory);

// Copies the default logger into a new file for this run, so each of two windows on one
// machine keeps its own whole log.
std::expected<std::filesystem::path, std::string> AddLogFile(const std::filesystem::path& directory);

}  // namespace z13
