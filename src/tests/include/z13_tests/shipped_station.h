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

#include <lib_core/utils/asset_source.h>
#include <z13/components/block.h>
#include <primitives/blueprint.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <primitives/station_assets.h>
#include <rooms/portal_source.h>
#include <rooms/room_sources.h>

namespace z13::testing {

// assets/station/blueprints/site.json: a floor with one spawn point.
inline constexpr std::string_view kSiteScene = "site";
// assets/station/blueprints/test.json, from docs/station-layout/generate_layout.py.
inline constexpr std::string_view kTestScene = "test";
inline constexpr std::string_view kSpireScene = z13::building::primitives::kFeaturedScene;

// The source tree's assets/ and config/ (Z13_SOURCE_ASSETS_DIR, set for the test runner).
inline z13::AssetSource SourceAssets() {
  return z13::AssetSource(std::filesystem::path(Z13_SOURCE_ASSETS_DIR).parent_path());
}

inline std::vector<std::string> ShippedScenes() {
  return z13::building::primitives::BlueprintScenes(
      SourceAssets().ListAssets(z13::building::primitives::BlueprintsDir()));
}

inline std::expected<z13::building::primitives::Palette, std::string> ShippedPalette() {
  return SourceAssets().ReadAsset(z13::building::primitives::PaletteFile())
      .and_then([](const std::string& json) { return z13::building::primitives::ParsePalette(json); });
}

inline std::expected<std::vector<z13::station::Block>, std::string> ShippedBlueprint(
    std::string_view scene, const z13::building::primitives::Palette& palette) {
  return SourceAssets().ReadAsset(z13::building::primitives::BlueprintFile(scene))
      .and_then([&palette](const std::string& json) { return z13::building::primitives::ParseBlueprint(json, palette); });
}

// What the room build reads from a station's blocks: the sealing ones and their portals.
inline z13::station::rooms::RoomSources RoomSourcesOf(const std::vector<z13::station::Block>& blocks,
                                                      const z13::building::primitives::Palette& palette) {
  z13::station::rooms::RoomSources sources;
  for (const z13::station::Block& block : blocks) {
    const z13::building::primitives::Primitive& primitive = palette.Find(block.spec.type_id)->get();
    if (!z13::station::rooms::IsSealing(primitive)) {
      continue;
    }
    sources.sealed.push_back(z13::building::primitives::OccupiedCells(block));
    if (const auto portal = z13::station::rooms::PortalOf(block, primitive)) {
      sources.portals.push_back(*portal);
    }
  }
  return sources;
}

}  // namespace z13::testing
