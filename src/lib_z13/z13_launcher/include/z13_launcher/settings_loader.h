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

#include <z13_settings/settings.h>

namespace z13 {

// A missing key keeps its default; an unknown key or an out-of-range value is an error.
std::expected<Settings, std::string> ParseSettings(const std::string& json_text);

// Every field is written out, so the file shows what can be tuned.
std::expected<std::string, std::string> SerializeSettings(const Settings& settings);

// Reads and parses `config_path`. A missing file is created from the schema defaults; failing
// to write it is only logged, the defaults are used either way.
std::expected<Settings, std::string> ReadSettings(const std::filesystem::path& config_path);

}  // namespace z13
