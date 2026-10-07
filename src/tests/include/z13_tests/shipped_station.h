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
#include <string_view>
#include <vector>

#include <z13/components/station.h>
#include <z13_primitives/blueprint.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/station_assets.h>

namespace z13::testing {

// The station assets in the source tree (Z13_SOURCE_ASSETS_DIR, set for the test runner).
inline std::filesystem::path SourceAsset(const std::filesystem::path& relative) {
  return std::filesystem::path(Z13_SOURCE_ASSETS_DIR) / relative;
}

inline std::expected<z13::building::primitives::Palette, std::string> ShippedPalette() {
  return z13::building::primitives::ReadTextFile(SourceAsset(z13::building::primitives::PaletteFile()))
      .and_then([](const std::string& json) { return z13::building::primitives::ParsePalette(json); });
}

inline std::expected<std::vector<z13::station::Block>, std::string> ShippedBlueprint(
    std::string_view scene, const z13::building::primitives::Palette& palette) {
  return z13::building::primitives::ReadTextFile(SourceAsset(z13::building::primitives::BlueprintFile(scene)))
      .and_then([&palette](const std::string& json) { return z13::building::primitives::ParseBlueprint(json, palette); });
}

}  // namespace z13::testing
