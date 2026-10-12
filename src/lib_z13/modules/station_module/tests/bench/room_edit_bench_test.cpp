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

// The tick that builds or destroys one wall in each shipped station, system by system,
// without rendering. Built by `make.py --bench`, run with
// `z13.py --bench --filter 'RoomEditBench.*'`.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/utils/system_times.h>

#include <z13/components/station.h>
#include <primitives/station_assets.h>
#include <rooms/room_cache.h>
#include <z13_tests/shipped_station.h>

#include "../../../z13_module/tests/support/z13_test_world.h"
#include "../support/room_blocks.h"

namespace z13::testing {
namespace {

using z13::station::rooms::RoomCache;
using z13::station::rooms::RoomGraph;

constexpr int kEdits = 3;
constexpr size_t kTopSystems = 6;
// A wall panel's size in cells: a 1 m square, one cell thick.
const Eigen::Vector3i kPanel {4, 1, 4};

template <typename F>
double Ms(F&& work) {
  const auto start = std::chrono::steady_clock::now();
  work();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

const RoomGraph& Graph(Z13TestWorld& world) {
  return world.World().get<RoomCache>().Current()->get();
}

// The anchor of the largest room: a panel there changes the topology without touching the hull.
Eigen::Vector3i LargestRoomAnchor(const RoomGraph& graph) {
  const std::vector<z13::station::rooms::RoomId> rooms = graph.ByAnchor();
  const auto largest = std::ranges::max_element(
      rooms, {}, [&graph](z13::station::rooms::RoomId room) { return graph.rooms.at(room).volume_cells; });
  return graph.rooms.at(*largest).anchor;
}

void PrintTick(const std::string& label, double ms, const std::vector<SystemTime>& systems) {
  std::cout << std::format("[ bench ] {}: tick {:.1f} ms;", label, ms);
  for (size_t i = 0; i < std::min(systems.size(), kTopSystems); ++i) {
    std::cout << std::format(" {} {:.1f};", systems[i].path, systems[i].ms);
  }
  std::cout << "\n" << std::flush;
}

double TimedTick(Z13TestWorld& world, const std::string& label) {
  ecs_measure_system_time(world.World(), true);
  const auto before = SystemTimes(world.World());
  const double ms = Ms([&] { world.Tick(); });
  PrintTick(label, ms, SystemTimesPerFrame(before, SystemTimes(world.World()), 1));
  ecs_measure_system_time(world.World(), false);
  return ms;
}

TEST(RoomEditBench, DISABLED_BuildAndDestroyAWall) {
  for (const std::string& scene : z13::building::primitives::BlueprintScenes(SourceAsset({}))) {
    Z13TestWorld world({std::string(kStationSceneArg), scene});
    world.Tick();
    world.Tick();
    const Eigen::Vector3i anchor = LargestRoomAnchor(Graph(world));
    std::cout << std::format("[ bench ] '{}': {} rooms, {} intervals\n", scene, Graph(world).rooms.size(),
                             Graph(world).columns.Intervals().size());
    for (int edit = 0; edit < kEdits; ++edit) {
      const z13::station::Block panel {
          .spec = {.type_id = z13::station::testing::IdOf(z13::station::testing::kWall), .size = kPanel},
          .cell = anchor + Eigen::Vector3i(0, 0, edit)};
      flecs::entity built = z13::station::testing::AddBlock(world, panel);
      TimedTick(world, std::format("'{}' build {}", scene, edit));
      built.destruct();
      TimedTick(world, std::format("'{}' destroy {}", scene, edit));
    }
  }
}

}  // namespace
}  // namespace z13::testing
