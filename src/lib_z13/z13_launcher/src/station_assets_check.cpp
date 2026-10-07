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

#include <z13_launcher/station_assets_check.h>

#include <format>

#include <lib_core/utils/file_io.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/station_assets.h>

namespace z13 {

Status CheckStationScene(const Config& config, const std::filesystem::path& assets_dir) {
  const auto scene = config.GetStationScene();
  if (!scene) {
    return {};
  }
  const std::filesystem::path file = assets_dir / z13::building::primitives::BlueprintFile(*scene);
  if (!std::filesystem::is_regular_file(file)) {
    return std::unexpected(std::format("--station-scene {}: no blueprint {}", *scene, file.string()));
  }
  return {};
}

Status CheckBlockPalette(const std::filesystem::path& assets_dir) {
  const std::filesystem::path file = assets_dir / z13::building::primitives::PaletteFile();
  return ReadFile(file)
      .and_then([](const std::string& json) { return z13::building::primitives::ParsePalette(json); })
      .transform([](const auto&) {})
      .transform_error([&file](const std::string& error) {
        return std::format("no block palette: {} ({})", error, file.string());
      });
}

}  // namespace z13
