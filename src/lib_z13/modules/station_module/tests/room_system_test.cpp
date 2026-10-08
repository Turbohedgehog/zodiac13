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

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>

#include <z13/components/gameplay.h>
#include <z13/components/rooms.h>
#include <z13/components/station.h>
#include <primitives/palette.h>
#include <primitives/station_assets.h>
#include <rooms/room_cache.h>
#include <z13_tests/shipped_station.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/z13_test_world.h"

namespace z13::station {
namespace {

using z13::station::rooms::RoomCache;
using z13::station::rooms::RoomGraph;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr std::string_view kFloor = "Floor";
constexpr std::string_view kWall = "Wall";
constexpr std::string_view kDoor = "Door";

// Far from anything the site scene builds.
const Eigen::Vector3i kRoomCorner {30, 30, 5};
constexpr int kSide = 10;
constexpr int kHeight = 12;
// The door primitive's frame and its 4x8 opening.
constexpr int kDoorWidth = 6;
constexpr int kDoorHeight = 10;
constexpr int kDoorOpeningCells = 4 * 8;

uint32_t IdOf(std::string_view name) {
  return z13::testing::ShippedPalette()->Find(name)->get().id;
}

// Wall panels along y: the primitive's x runs along the world's y.
constexpr Orientation kAlongY = Orientation::kFacePosYUpPosZ;

Block At(std::string_view primitive, Eigen::Vector3i size, Orientation orientation, Eigen::Vector3i offset) {
  return {.spec = {.type_id = IdOf(primitive), .size = size, .orientation = orientation},
          .cell = kRoomCorner + offset};
}

// A sealed box whose inside is kSide x kSide x kHeight cells, with its lowest cell at (1, 1, 1).
std::vector<Block> SealedBox() {
  constexpr int outer = kSide + 2;
  return {
      At(kFloor, {outer, outer, 1}, {}, {0, 0, 0}),
      At(kFloor, {outer, outer, 1}, {}, {0, 0, kHeight + 1}),
      At(kWall, {outer, 1, kHeight}, {}, {0, 0, 1}),
      At(kWall, {outer, 1, kHeight}, {}, {0, outer - 1, 1}),
      At(kWall, {kSide, 1, kHeight}, kAlongY, {0, 1, 1}),
      At(kWall, {kSide, 1, kHeight}, kAlongY, {outer - 1, 1, 1}),
  };
}

// A partition at x = 5 with a door in it, leaving the box as two rooms.
std::vector<Block> DoorPartition() {
  return {
      At(kDoor, {kDoorWidth, 1, kDoorHeight}, kAlongY, {5, 1, 1}),
      At(kWall, {kSide - kDoorWidth, 1, kHeight}, kAlongY, {5, 1 + kDoorWidth, 1}),
      At(kWall, {kDoorWidth, 1, kHeight - kDoorHeight}, kAlongY, {5, 1, 1 + kDoorHeight}),
  };
}

flecs::entity AddBlock(Z13TestWorld& world, const Block& block) {
  // Named, as the game names its blocks: a restore matches state entities by name.
  const std::string name = std::format("RoomTest_{}_{}_{}_{}", block.spec.type_id, block.cell.x(), block.cell.y(), block.cell.z());
  return world.World().entity(name.c_str()).add<z13::flecs_tools::StateEntity>().set(block);
}

void AddBlocks(Z13TestWorld& world, const std::vector<Block>& blocks) {
  for (const Block& block : blocks) {
    AddBlock(world, block);
  }
}

const RoomGraph& Graph(Z13TestWorld& world) {
  return world.World().get<RoomCache>().Current()->get();
}

uint64_t Version(Z13TestWorld& world) {
  return world.World().get<TopologyVersion>().version;
}

TEST(RoomSystemTest, ABoxOfBlocksIsOneRoom) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());

  world.Tick();

  const RoomGraph& graph = Graph(world);
  const auto inside = graph.RoomAt(kRoomCorner + Eigen::Vector3i(1, 1, 1));
  ASSERT_TRUE(inside.has_value());
  EXPECT_NE(*inside, rooms::kVacuumRoom);
  EXPECT_EQ(graph.rooms[*inside].volume_cells, kSide * kSide * kHeight);
  EXPECT_EQ(graph.rooms[*inside].anchor, kRoomCorner + Eigen::Vector3i(1, 1, 1));
}

TEST(RoomSystemTest, ADoorPartitionSplitsTheBoxAndLeavesAClosedPortal) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  AddBlocks(world, DoorPartition());

  world.Tick();

  const RoomGraph& graph = Graph(world);
  const auto west = graph.RoomAt(kRoomCorner + Eigen::Vector3i(1, 5, 5));
  const auto east = graph.RoomAt(kRoomCorner + Eigen::Vector3i(8, 5, 5));
  ASSERT_TRUE(west && east);
  EXPECT_NE(*west, *east);
  const auto portal = std::ranges::find_if(graph.portals, [&](const rooms::Portal& p) {
    return std::min(p.a, p.b) == std::min(*west, *east) && std::max(p.a, p.b) == std::max(*west, *east);
  });
  ASSERT_NE(portal, graph.portals.end());
  EXPECT_EQ(portal->area_cells, kDoorOpeningCells);
  EXPECT_FALSE(portal->passable);
  EXPECT_FALSE(portal->visible);
}

TEST(RoomSystemTest, TheOverlayShowsABoxShapedRoomAsOneBox) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  world.Tick();
  world.World().get_mut<RoomOverlay>().enabled = true;
  Z13TestWorld& w = world;
  w.World().query_builder<const z13::gameplay::Player, Eigen::Matrix4f>().build().each(
      [](const z13::gameplay::Player&, Eigen::Matrix4f& transform) {
        transform.col(3).head<3>() = (kRoomCorner + Eigen::Vector3i(5, 5, 5)).cast<float>() * kCellSize;
      });

  world.Tick();

  const auto& boxes = world.World().get<RoomOverlay>().boxes;
  ASSERT_EQ(boxes.size(), 1U);
  EXPECT_EQ(boxes.front().min, kRoomCorner + Eigen::Vector3i(1, 1, 1));
  EXPECT_EQ(boxes.front().extent, Eigen::Vector3i(kSide, kSide, kHeight));
}

TEST(RoomSystemTest, TheVersionGrowsOnlyWhenTheBlocksChange) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  const uint64_t start = Version(world);

  world.Tick();
  EXPECT_EQ(Version(world), start);

  const flecs::entity wall = AddBlock(world, SealedBox().front());
  world.Tick();
  EXPECT_EQ(Version(world), start + 1);

  world.Tick();
  EXPECT_EQ(Version(world), start + 1);

  wall.destruct();
  world.Tick();
  EXPECT_EQ(Version(world), start + 2);
}

TEST(RoomSystemTest, ABlockThatSealsNothingLeavesTheVersionAlone) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  const uint64_t start = Version(world);

  AddBlock(world, {.spec = CubeSpec(), .cell = kRoomCorner});
  world.Tick();

  EXPECT_EQ(Version(world), start);
}

TEST(RoomSystemTest, ARestoredSnapshotGetsTheRoomsOfItsBlocks) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  world.Tick();
  const auto snapshot = z13::flecs_tools::CaptureState(world.World());
  ASSERT_TRUE(snapshot.has_value()) << snapshot.error();
  const uint64_t version = Version(world);
  const size_t rooms = Graph(world).rooms.size();
  AddBlocks(world, DoorPartition());
  world.Tick();
  ASSERT_GT(Graph(world).rooms.size(), rooms);

  ASSERT_TRUE(z13::flecs_tools::RestoreWorld(world.World(), *snapshot).has_value());
  world.Tick();

  EXPECT_EQ(Graph(world).rooms.size(), rooms);
  EXPECT_EQ(Version(world), version);
}

TEST(RoomSystemTest, ReplayingTheSameBuildsFromASnapshotCountsTheSameVersion) {
  Z13TestWorld first = StationWorld();
  first.Tick();
  const auto snapshot = z13::flecs_tools::CaptureState(first.World());
  ASSERT_TRUE(snapshot.has_value()) << snapshot.error();
  AddBlocks(first, SealedBox());
  first.Tick();
  AddBlocks(first, DoorPartition());
  first.Tick();

  Z13TestWorld replay = StationWorld();
  replay.Tick();
  ASSERT_TRUE(z13::flecs_tools::RestoreWorld(replay.World(), *snapshot).has_value());
  AddBlocks(replay, SealedBox());
  replay.Tick();
  AddBlocks(replay, DoorPartition());
  replay.Tick();

  EXPECT_EQ(Version(replay), Version(first));
  EXPECT_EQ(Graph(replay).rooms, Graph(first).rooms);
  EXPECT_EQ(Graph(replay).portals.size(), Graph(first).portals.size());
}

// Every blueprint in the source assets: a new one is tested without touching the code.
class ShippedRoomsTest : public ::testing::TestWithParam<std::string> {};

TEST_P(ShippedRoomsTest, TheStationHasRoomsAndEveryPortalJoinsTwoOfThem) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), GetParam()});
  world.Tick();

  const auto current = world.World().get<RoomCache>().Current();
  ASSERT_TRUE(current.has_value());
  const RoomGraph& graph = current->get();
  EXPECT_GT(Version(world), 0U);
  EXPECT_TRUE(std::ranges::all_of(graph.portals, [&graph](const rooms::Portal& portal) {
    return portal.a != portal.b && portal.a < graph.rooms.size() && portal.b < graph.rooms.size() &&
           portal.area_cells > 0;
  }));
}

TEST_P(ShippedRoomsTest, EverySpawnPointIsInARoomOfItsOwn) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), GetParam()});
  world.Tick();
  const RoomGraph& graph = Graph(world);
  if (graph.rooms.size() < 2) {
    GTEST_SKIP() << "no rooms to spawn in";
  }

  world.World().query_builder<const SpawnPoint>().build().each([&](flecs::entity e, const SpawnPoint& point) {
    const Eigen::Vector3i cell = (point.transform.col(3).head<3>() / kCellSize).array().floor().cast<int>();
    const auto room = graph.RoomAt(cell);
    EXPECT_TRUE(room.has_value() && *room != rooms::kVacuumRoom)
        << e.name() << " at cell " << cell.transpose() << " is " << (room ? "in the vacuum" : "inside a block");
  });
}

TEST_P(ShippedRoomsTest, TheOverlayBoxesOfTheRoomCoverExactlyItsCells) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), GetParam()});
  world.Tick();
  if (Graph(world).rooms.size() < 2) {
    GTEST_SKIP() << "no rooms";
  }
  world.World().get_mut<RoomOverlay>().enabled = true;
  world.Tick();

  const RoomGraph& graph = Graph(world);
  int64_t covered = 0;
  std::optional<rooms::RoomIndex> room;
  for (const OverlayBox& box : world.World().get<RoomOverlay>().boxes) {
    if (box.color != kRoomOverlayColor) {
      continue;
    }
    for (int z = box.min.z(); z < box.min.z() + box.extent.z(); ++z) {
      for (int y = box.min.y(); y < box.min.y() + box.extent.y(); ++y) {
        for (int x = box.min.x(); x < box.min.x() + box.extent.x(); ++x) {
          const auto here = graph.RoomAt({x, y, z});
          room = room.has_value() ? room : here;
          ASSERT_EQ(here, room) << "cell " << x << " " << y << " " << z;
          ++covered;
        }
      }
    }
  }
  ASSERT_TRUE(room.has_value());
  EXPECT_EQ(covered, graph.rooms[*room].volume_cells);
}

INSTANTIATE_TEST_SUITE_P(
    Scenes, ShippedRoomsTest,
    ::testing::ValuesIn(z13::building::primitives::BlueprintScenes(z13::testing::SourceAsset({}))),
    [](const ::testing::TestParamInfo<std::string>& info) { return info.param; });

}  // namespace
}  // namespace z13::station
