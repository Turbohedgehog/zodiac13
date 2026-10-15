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
#include <z13_tests/pool_parallel.h>
#include <z13_tests/shipped_station.h>
#include <z13/components/block.h>

namespace z13::testing {
namespace {

namespace primitives = z13::building::primitives;

constexpr int kRuns = 5;
// CoreSettings::threads' default.
constexpr size_t kPoolThreads = 4;

TEST(RoomBuildBench, DISABLED_FullRebuild) {
  const primitives::Palette palette = ShippedPalette().value();
  for (const std::string& scene : z13::testing::ShippedScenes()) {
    const auto blocks = ShippedBlueprint(scene, palette);
    ASSERT_TRUE(blocks.has_value()) << blocks.error();
    const z13::station::rooms::RoomSources sources = RoomSourcesOf(*blocks, palette);
    const z13::ThreadPool pool(kPoolThreads);
    const auto ms_since = [](std::chrono::steady_clock::time_point start) {
      return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    for (int run = 0; run < kRuns; ++run) {
      auto start = std::chrono::steady_clock::now();
      const auto graph = z13::station::rooms::BuildRooms(sources);
      const double serial_ms = ms_since(start);
      start = std::chrono::steady_clock::now();
      const auto pooled = z13::station::rooms::BuildRooms(sources, z13::station::rooms::kVacuumRoom + 1, {},
                                                          OnPool(pool));
      const double pooled_ms = ms_since(start);
      ASSERT_TRUE(graph.has_value()) << graph.error();
      ASSERT_TRUE(pooled.has_value()) << pooled.error();
      std::cout << std::format(
          "[ bench ] '{}' run {}: {} sealing blocks, {} rooms, {} portals, {} free leaves, built in {:.1f} ms, "
          "{:.1f} ms on {} threads\n",
          scene, run, sources.sealed.size(), graph->rooms.size(), graph->portals.size(), graph->space.LeafCount(),
          serial_ms, pooled_ms, kPoolThreads)
                << std::flush;
    }
  }
}

}  // namespace
}  // namespace z13::testing
