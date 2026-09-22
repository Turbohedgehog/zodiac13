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

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace z13 {

// Parses a `modules:` sequence (bare path, or {path: ...}); a malformed entry is
// logged and skipped. Split out from ReadModuleList() so it's testable without a file.
std::expected<std::vector<std::string>, std::string> ParseModuleList(const std::string& yaml_text);

// Reads and parses `config_path`.
std::expected<std::vector<std::string>, std::string> ReadModuleList(const std::filesystem::path& config_path);

}  // namespace z13
