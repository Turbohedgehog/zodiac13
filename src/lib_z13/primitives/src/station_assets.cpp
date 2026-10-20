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

#include <primitives/station_assets.h>

#include <algorithm>
#include <format>

namespace z13::building::primitives {

std::filesystem::path PaletteFile() {
  return std::filesystem::path(kStationDir) / kPaletteFileName;
}

std::filesystem::path BlueprintsDir() {
  return std::filesystem::path(kStationDir) / kBlueprintsDir;
}

std::filesystem::path BlueprintFile(std::string_view scene) {
  return BlueprintsDir() / std::format("{}{}", scene, kBlueprintExtension);
}

std::filesystem::path DecorFile(std::string_view scene) {
  return std::filesystem::path(kStationDir) / kDecorDir / std::format("{}{}", scene, kBlueprintExtension);
}

std::filesystem::path PropModelFile(std::string_view model) {
  return std::filesystem::path(kPropModelsDir) / std::format("{}{}", model, kPropModelExtension);
}

std::filesystem::path TextureFile(std::string_view texture) {
  return std::filesystem::path(kTexturesDir) / std::format("{}{}", texture, kTextureExtension);
}

std::vector<std::string> BlueprintScenes(std::span<const std::string> file_names) {
  std::vector<std::string> scenes;
  for (const std::filesystem::path name : file_names) {
    if (name.extension() == kBlueprintExtension) {
      scenes.push_back(name.stem().string());
    }
  }
  std::ranges::sort(scenes);
  return scenes;
}

}  // namespace z13::building::primitives
