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
#include <string>
#include <string_view>
#include <vector>

namespace z13::building::primitives {

// Header constants rather than path objects in the .cpp: this library is linked into several
// binaries, and check_duplicated_state.py allows only these.
inline constexpr std::string_view kStationDir = "station";
inline constexpr std::string_view kPaletteFileName = "palette.json";
inline constexpr std::string_view kBlueprintsDir = "blueprints";
inline constexpr std::string_view kBlueprintExtension = ".json";

// Station asset files, relative to assets/.
std::filesystem::path PaletteFile();
std::filesystem::path BlueprintFile(std::string_view scene);

// The scenes whose blueprints lie in `assets_dir`, sorted.
std::vector<std::string> BlueprintScenes(const std::filesystem::path& assets_dir);

}  // namespace z13::building::primitives
