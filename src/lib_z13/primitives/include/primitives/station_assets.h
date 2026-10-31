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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace z13::building::primitives {

// Header constants rather than path objects in the .cpp: this library is linked into several
// binaries, and check_duplicated_state.py allows only these.
inline constexpr std::string_view kStationDir = "station";
inline constexpr std::string_view kPaletteFileName = "palette.json";
inline constexpr std::string_view kPaletteFragmentsDir = "palette";
inline constexpr std::string_view kBlueprintsDir = "blueprints";
inline constexpr std::string_view kBlueprintExtension = ".json";
inline constexpr std::string_view kDecorDir = "decor";
inline constexpr std::string_view kTexturesDir = "textures/station";
inline constexpr std::string_view kTextureExtension = ".png";
inline constexpr std::string_view kPropModelsDir = "models/props";
inline constexpr std::string_view kPropModelExtension = ".glb";
// The largest shipped station.
inline constexpr std::string_view kFeaturedScene = "spire";
// The station the menu offers first.
inline constexpr std::string_view kDefaultScene = "hub";
// The station most of the render measuring is done in.
inline constexpr std::string_view kOctantScene = "octant";

// Station asset files, relative to assets/.
std::filesystem::path PaletteFile();
// A utility module's primitives, named by the module.
std::filesystem::path PaletteFragmentsDir();
std::filesystem::path PaletteFragmentFile(std::string_view module);
std::filesystem::path BlueprintsDir();
std::filesystem::path BlueprintFile(std::string_view scene);
// The props of `scene`, and the model file of a prop.
std::filesystem::path DecorFile(std::string_view scene);
std::filesystem::path PropModelFile(std::string_view model);
std::filesystem::path TextureFile(std::string_view texture);

// The scenes among the file names in BlueprintsDir(), sorted.
std::vector<std::string> BlueprintScenes(std::span<const std::string> file_names);

}  // namespace z13::building::primitives
