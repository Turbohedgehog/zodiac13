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
#include <system_error>

namespace z13::building::primitives {

namespace {

std::filesystem::path BlueprintsDir() {
  return std::filesystem::path(kStationDir) / kBlueprintsDir;
}

}  // namespace

std::filesystem::path PaletteFile() {
  return std::filesystem::path(kStationDir) / kPaletteFileName;
}

std::filesystem::path BlueprintFile(std::string_view scene) {
  return BlueprintsDir() / std::format("{}{}", scene, kBlueprintExtension);
}

std::vector<std::string> BlueprintScenes(const std::filesystem::path& assets_dir) {
  std::vector<std::string> scenes;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(assets_dir / BlueprintsDir(), error)) {
    if (entry.is_regular_file() && entry.path().extension() == kBlueprintExtension) {
      scenes.push_back(entry.path().stem().string());
    }
  }
  std::ranges::sort(scenes);
  return scenes;
}

}  // namespace z13::building::primitives
