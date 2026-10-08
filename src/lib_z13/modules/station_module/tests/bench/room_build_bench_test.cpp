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

// Full rebuild of the rooms of every shipped station (rooms/room_builder.h). Built by
// `make.py --bench`, run with `z13.py --bench --filter 'RoomBuildBench.*'`.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <string>

#include <primitives/station_assets.h>
#include <rooms/portal_source.h>
#include <rooms/room_builder.h>
#include <z13_tests/shipped_station.h>

namespace z13::testing {
namespace {

namespace primitives = z13::building::primitives;

constexpr int kRuns = 5;

TEST(RoomBuildBench, DISABLED_FullRebuild) {
  const primitives::Palette palette = ShippedPalette().value();
  for (const std::string& scene : primitives::BlueprintScenes(SourceAsset({}))) {
    const auto blocks = ShippedBlueprint(scene, palette);
    ASSERT_TRUE(blocks.has_value()) << blocks.error();
    z13::station::rooms::RoomSources sources;
    for (const z13::station::Block& block : *blocks) {
      const primitives::Primitive& primitive = palette.Find(block.spec.type_id)->get();
      if (!z13::station::rooms::IsSealing(primitive)) {
        continue;
      }
      sources.sealed.push_back(primitives::OccupiedCells(block));
      if (const auto portal = z13::station::rooms::PortalOf(block, primitive)) {
        sources.portals.push_back(*portal);
      }
    }
    for (int run = 0; run < kRuns; ++run) {
      const auto start = std::chrono::steady_clock::now();
      const auto graph = z13::station::rooms::BuildRooms(sources);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      ASSERT_TRUE(graph.has_value()) << graph.error();
      std::cout << std::format(
          "[ bench ] '{}' run {}: {} sealing blocks, {} rooms, {} portals, {} intervals, built in {:.1f} ms\n", scene,
          run, sources.sealed.size(), graph->rooms.size(), graph->portals.size(), graph->columns.Intervals().size(),
          ms)
                << std::flush;
    }
  }
}

}  // namespace
}  // namespace z13::testing
