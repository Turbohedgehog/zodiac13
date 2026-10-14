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

#include <primitives/palette.h>
#include <primitives/station_assets.h>

namespace z13 {

Status CheckStationScene(const Config& config, const AssetSource& assets) {
  const auto scene = config.GetStationScene();
  if (!scene) {
    return {};
  }
  return assets.ReadAsset(z13::building::primitives::BlueprintFile(*scene))
      .transform([](const std::string&) {})
      .transform_error([&scene](const std::string& error) {
        return std::format("--station-scene {}: no blueprint: {}", *scene, error);
      });
}

Status CheckBlockPalette(const AssetSource& assets) {
  return assets.ReadAsset(z13::building::primitives::PaletteFile())
      .and_then([](const std::string& json) { return z13::building::primitives::ParsePalette(json); })
      .transform([](const auto&) {})
      .transform_error([](const std::string& error) {
        return std::format("no block palette: {} ({})", error, z13::building::primitives::PaletteFile().string());
      });
}

}  // namespace z13
