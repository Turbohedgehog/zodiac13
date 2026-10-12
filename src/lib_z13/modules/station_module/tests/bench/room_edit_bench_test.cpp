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
#include <optional>
#include <string>
#include <utility>
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
const Eigen::Vector3i kCube {1, 1, 1};

template <typename F>
double Ms(F&& work) {
  const auto start = std::chrono::steady_clock::now();
  work();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

const RoomGraph& Graph(Z13TestWorld& world) {
  return world.World().get<RoomCache>().Current()->get();
}

// The anchor of the largest or the smallest room: a block there changes the topology
// without touching the hull. Nothing for a scene without rooms.
std::optional<Eigen::Vector3i> RoomAnchor(const RoomGraph& graph, bool largest) {
  const std::vector<z13::station::rooms::RoomId> rooms = graph.ByAnchor();
  if (rooms.empty()) {
    return std::nullopt;
  }
  const auto volume = [&graph](z13::station::rooms::RoomId room) { return graph.rooms.at(room).volume_cells; };
  const auto found = largest ? std::ranges::max_element(rooms, {}, volume) : std::ranges::min_element(rooms, {}, volume);
  return graph.rooms.at(*found).anchor;
}

z13::station::Block WallAt(const Eigen::Vector3i& cell, const Eigen::Vector3i& size) {
  return {.spec = {.type_id = z13::station::testing::IdOf(z13::station::testing::kWall), .size = size}, .cell = cell};
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

// Builds the block and takes it away again, timing both ticks.
void BuildAndDestroy(Z13TestWorld& world, const std::string& label, const z13::station::Block& block) {
  flecs::entity built = z13::station::testing::AddBlock(world, block);
  TimedTick(world, std::format("{}: build", label));
  built.destruct();
  TimedTick(world, std::format("{}: destroy", label));
}

// Destroys one of the station's own walls, which no cached graph knows, and puts it back.
void DestroyAndRestore(Z13TestWorld& world, const std::string& label) {
  std::optional<std::pair<flecs::entity, z13::station::Block>> wall;
  const uint32_t wall_type = z13::station::testing::IdOf(z13::station::testing::kWall);
  world.World().each([&](flecs::entity entity, const z13::station::Block& block) {
    if (!wall && block.spec.type_id == wall_type) {
      wall.emplace(entity, block);
    }
  });
  if (!wall) {
    return;
  }
  wall->first.destruct();
  TimedTick(world, std::format("{}: destroy", label));
  z13::station::testing::AddBlock(world, wall->second);
  TimedTick(world, std::format("{}: restore", label));
}

TEST(RoomEditBench, DISABLED_BuildAndDestroyAWall) {
  for (const std::string& scene : z13::building::primitives::BlueprintScenes(SourceAsset({}))) {
    Z13TestWorld world({std::string(kStationSceneArg), scene});
    world.Tick();
    world.Tick();
    const auto largest = RoomAnchor(Graph(world), true);
    const auto smallest = RoomAnchor(Graph(world), false);
    if (!largest || !smallest) {
      continue;
    }
    std::cout << std::format("[ bench ] '{}': {} rooms, {} free leaves\n", scene, Graph(world).rooms.size(),
                             Graph(world).space.LeafCount());
    for (int edit = 0; edit < kEdits; ++edit) {
      const Eigen::Vector3i up(0, 0, edit);
      BuildAndDestroy(world, std::format("'{}' largest room {}", scene, edit), WallAt(*largest + up, kPanel));
      BuildAndDestroy(world, std::format("'{}' smallest room {}", scene, edit), WallAt(*smallest + up, kCube));
      BuildAndDestroy(world, std::format("'{}' outside the hull {}", scene, edit),
                      WallAt(Graph(world).space.Bounds().min + up, kCube));
      DestroyAndRestore(world, std::format("'{}' station wall {}", scene, edit));
    }
  }
}

}  // namespace
}  // namespace z13::testing
