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

#include <filesystem>
#include <string_view>

namespace z13::tools::environment {

// Overrides the data directory (settings, input config, saves) when set.
constexpr std::string_view kDataDirectoryVariable = "Z13_DATA_DIR";

std::filesystem::path GetGameDataDirectory();
std::filesystem::path GetGameInputConfigJsonPath();
std::filesystem::path GetGameInputConfigJsonPath2();
// Next to the input config; generated from the schema defaults when missing.
std::filesystem::path GetGameSettingsJsonPath();
std::filesystem::path GetGameQuickSaveJsonPath();
std::filesystem::path GetGameLogsDirectory();

} // namespace z13::environment