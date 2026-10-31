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
#include <numeric>
#include <ranges>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>
#include <lib_core/utils/enum_cycle.h>

#include <z13/components/door.h>
#include <z13/components/rooms.h>
#include <z13/components/block.h>
#include <z13/components/spawn_point.h>
#include <primitives/palette.h>
#include <primitives/station_assets.h>
#include <rooms/room_cache.h>
#include <z13_tests/shipped_station.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "support/room_blocks.h"

namespace z13::station {
namespace {

using z13::station::rooms::RoomCache;
using z13::station::rooms::RoomGraph;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

using testing::AddBlock;
using testing::AddBlocks;
using testing::At;
using testing::DoorPartition;
using testing::IdOf;
using testing::kAlongY;
using testing::kDoor;
using testing::kDoorHeight;
using testing::kDoorOpeningCells;
using testing::kDoorWidth;
using testing::kFloor;
using testing::kHeight;
using testing::kRoomCorner;
using testing::kSide;
using testing::kWall;
using testing::kWindow;
using testing::SealedBox;

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
  EXPECT_EQ(graph.rooms.at(*inside).volume_cells, kSide * kSide * kHeight);
  EXPECT_EQ(graph.rooms.at(*inside).anchor, kRoomCorner + Eigen::Vector3i(1, 1, 1));
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

// The renderer isn't in the tests: they name the seen rooms themselves.
void ShowRooms(Z13TestWorld& world, RoomOverlayMode mode, std::optional<std::vector<uint32_t>> seen = std::nullopt,
               uint64_t fingerprint_offset = 0) {
  std::optional<RoomsDrawn> drawn;
  if (seen) {
    const uint64_t fingerprint = world.World().get<RoomCache>().CurrentFingerprint().value_or(0) + fingerprint_offset;
    drawn = RoomsDrawn {.fingerprint = fingerprint, .rooms = std::move(*seen)};
  }
  world.World().set(RoomOverlay {.mode = mode, .drawn = std::move(drawn)});
  world.Tick();
}

size_t CountColoured(Z13TestWorld& world, const Rgba& color) {
  return static_cast<size_t>(std::ranges::count(world.World().get<RoomOverlay>().boxes, color, &OverlayBox::color));
}

int64_t RoomsVolume(Z13TestWorld& world) {
  const RoomGraph& graph = Graph(world);
  const std::vector<rooms::RoomId> inside_rooms = graph.ByAnchor();
  return std::accumulate(inside_rooms.begin(), inside_rooms.end(), int64_t {},
                         [&graph](int64_t sum, rooms::RoomId room) { return sum + graph.rooms.at(room).volume_cells; });
}

int64_t RoomBoxesVolume(Z13TestWorld& world) {
  int64_t volume = 0;
  for (const OverlayBox& box : world.World().get<RoomOverlay>().boxes) {
    if (box.color == kRoomOverlayColor) {
      volume += static_cast<int64_t>(box.extent.x()) * box.extent.y() * box.extent.z();
    }
  }
  return volume;
}

TEST(RoomSystemTest, TheOverlayShowsASeenBoxShapedRoomAsOneBox) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  world.Tick();
  const auto inside = Graph(world).RoomAt(kRoomCorner + Eigen::Vector3i(1, 1, 1));
  ASSERT_TRUE(inside.has_value());

  ShowRooms(world, RoomOverlayMode::kSeen, std::vector {*inside});

  const auto& boxes = world.World().get<RoomOverlay>().boxes;
  ASSERT_EQ(boxes.size(), 1U);
  EXPECT_EQ(boxes.front().min, kRoomCorner + Eigen::Vector3i(1, 1, 1));
  EXPECT_EQ(boxes.front().extent, Eigen::Vector3i(kSide, kSide, kHeight));
}

TEST(RoomSystemTest, ARoomWithASlopeIsOutlinedByTheSlopesEdgesNotByBoxes) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  AddBlock(world, At("Slope", {4, 4, 4}, {}, {1, 1, 1}));
  world.Tick();
  const auto inside = Graph(world).RoomAt(kRoomCorner + Eigen::Vector3i(kSide, kSide, kHeight));
  ASSERT_TRUE(inside.has_value());

  ShowRooms(world, RoomOverlayMode::kSeen, std::vector {*inside});

  const RoomOverlay& overlay = world.World().get<RoomOverlay>();
  EXPECT_EQ(overlay.outlined_rooms, std::vector<uint32_t> {*inside});
  EXPECT_TRUE(overlay.boxes.empty());
  const auto slanted = [](const OverlaySegment& segment) {
    const Eigen::Vector3f along = (segment.b - segment.a).cwiseAbs();
    return (along.array() > 0.01f).count() >= 2;
  };
  EXPECT_TRUE(std::ranges::any_of(overlay.segments, slanted));
}

TEST(RoomSystemTest, TheFloorAlongASlantedWallEndsAtItsPlaneWithoutStairs) {
  constexpr int kPanelRun = 8;
  constexpr int kPanelThickness = 3;
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  Block panel = At("Panel", {kPanelRun, kPanelThickness, kHeight}, Orientation::kFacePosXUpPosY, {1, 1, 1});
  panel.spec.slant = kPanelRun;
  AddBlock(world, panel);
  world.Tick();
  const auto inside = Graph(world).RoomAt(kRoomCorner + Eigen::Vector3i(kSide, 1, 3));
  ASSERT_TRUE(inside.has_value());

  ShowRooms(world, RoomOverlayMode::kSeen, std::vector {*inside});

  const float floor_level = static_cast<float>(kRoomCorner.z() + 1);
  const auto stair = [&](const OverlaySegment& segment) {
    const Eigen::Vector3f low = segment.a.cwiseMin(segment.b) - kRoomCorner.cast<float>();
    const Eigen::Vector3f high = segment.a.cwiseMax(segment.b) - kRoomCorner.cast<float>();
    const bool on_floor = segment.a.z() == floor_level && segment.b.z() == floor_level;
    const bool along_axis = low.x() == high.x() || low.y() == high.y();
    const bool beside_the_walls = low.x() >= 2.0f && high.x() <= kPanelRun && low.y() >= 2.0f && high.y() <= kSide;
    return on_floor && along_axis && beside_the_walls;
  };
  const RoomOverlay& overlay = world.World().get<RoomOverlay>();
  EXPECT_FALSE(std::ranges::any_of(overlay.segments, stair));
}

TEST(RoomSystemTest, TheOverlayShowsOnlyTheSeenRooms) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  AddBlocks(world, DoorPartition());
  world.Tick();
  const auto west = Graph(world).RoomAt(kRoomCorner + Eigen::Vector3i(1, 5, 5));
  ASSERT_TRUE(west.has_value());

  ShowRooms(world, RoomOverlayMode::kSeen, std::vector {*west});
  EXPECT_EQ(CountColoured(world, kSeenRoomOverlayColor), 1U);
  EXPECT_EQ(CountColoured(world, kRoomOverlayColor), 0U);
  EXPECT_EQ(CountColoured(world, kDoorOverlayColor), 0U);

  ShowRooms(world, RoomOverlayMode::kSeen);
  EXPECT_TRUE(world.World().get<RoomOverlay>().boxes.empty());
}

TEST(RoomSystemTest, TheOverlayMarksTheVisiblePortalsBetweenSeenRooms) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  AddBlock(world, At(kWindow, {kSide, 1, kHeight}, kAlongY, {5, 1, 1}));
  world.Tick();
  const auto west = Graph(world).RoomAt(kRoomCorner + Eigen::Vector3i(1, 5, 5));
  const auto east = Graph(world).RoomAt(kRoomCorner + Eigen::Vector3i(8, 5, 5));
  ASSERT_TRUE(west && east);

  ShowRooms(world, RoomOverlayMode::kSeen, std::vector {std::min(*west, *east), std::max(*west, *east)});
  EXPECT_EQ(CountColoured(world, kSeenPortalOverlayColor), 1U);

  ShowRooms(world, RoomOverlayMode::kSeen, std::vector {*west});
  EXPECT_EQ(CountColoured(world, kSeenPortalOverlayColor), 0U);

  ShowRooms(world, RoomOverlayMode::kAll);
  EXPECT_EQ(CountColoured(world, kSeenPortalOverlayColor), 0U);
  EXPECT_EQ(CountColoured(world, kWindowOverlayColor), 1U);
}

TEST(RoomSystemTest, TheOverlayIgnoresRoomsDrawnWithAnotherGraph) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  world.Tick();
  const auto inside = Graph(world).RoomAt(kRoomCorner + Eigen::Vector3i(1, 1, 1));
  ASSERT_TRUE(inside.has_value());

  ShowRooms(world, RoomOverlayMode::kSeen, std::vector {*inside}, 1);

  EXPECT_TRUE(world.World().get<RoomOverlay>().boxes.empty());
}

TEST(RoomSystemTest, TheOverlayShowsEveryRoomAndPortalInAllMode) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  AddBlocks(world, DoorPartition());
  world.Tick();

  ShowRooms(world, RoomOverlayMode::kAll);

  EXPECT_GE(CountColoured(world, kRoomOverlayColor), 2U);
  EXPECT_EQ(CountColoured(world, kDoorOverlayColor), 1U);
}

TEST(RoomSystemTest, TheOverlayFollowsTheRoomsAnEditChanges) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  world.Tick();
  ShowRooms(world, RoomOverlayMode::kAll);

  AddBlocks(world, DoorPartition());
  ShowRooms(world, RoomOverlayMode::kAll);

  EXPECT_EQ(Graph(world).ByAnchor().size(), 2U);
  EXPECT_EQ(RoomBoxesVolume(world), RoomsVolume(world));
}

TEST(RoomSystemTest, TheOverlayIsEmptyWhenOff) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  AddBlocks(world, SealedBox());
  world.Tick();
  ShowRooms(world, RoomOverlayMode::kAll);

  ShowRooms(world, RoomOverlayMode::kOff);

  EXPECT_TRUE(world.World().get<RoomOverlay>().boxes.empty());
  EXPECT_TRUE(world.World().get<RoomOverlay>().label.empty());
}

TEST(RoomSystemTest, F4StepsThroughSeenAllAndOff) {
  EXPECT_EQ(z13::NextEnumerator(RoomOverlayMode::kOff), RoomOverlayMode::kSeen);
  EXPECT_EQ(z13::NextEnumerator(RoomOverlayMode::kSeen), RoomOverlayMode::kAll);
  EXPECT_EQ(z13::NextEnumerator(RoomOverlayMode::kAll), RoomOverlayMode::kOff);
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

  DestroyBlock(wall, wall.get<Block>());
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
    return portal.a != portal.b && graph.rooms.contains(portal.a) && graph.rooms.contains(portal.b) &&
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
  ShowRooms(world, RoomOverlayMode::kSeen, std::vector<uint32_t> {1});
  if (!world.World().get<RoomOverlay>().outlined_rooms.empty()) {
    GTEST_SKIP() << "room 1 is drawn by its walls' edges";
  }

  const RoomGraph& graph = Graph(world);
  int64_t covered = 0;
  std::optional<rooms::RoomId> room;
  for (const OverlayBox& box : world.World().get<RoomOverlay>().boxes) {
    if (box.color != kSeenRoomOverlayColor) {
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
  EXPECT_EQ(covered, graph.rooms.at(*room).volume_cells);
}

TEST_P(ShippedRoomsTest, TheOverlayOfAllRoomsHoldsTheirWholeVolume) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), GetParam()});
  world.Tick();

  ShowRooms(world, RoomOverlayMode::kAll);

  const RoomGraph& graph = Graph(world);
  const auto& outlined = world.World().get<RoomOverlay>().outlined_rooms;
  const int64_t outlined_volume = std::accumulate(
      outlined.begin(), outlined.end(), int64_t {},
      [&graph](int64_t sum, rooms::RoomId room) { return sum + graph.rooms.at(room).volume_cells; });
  EXPECT_EQ(RoomBoxesVolume(world) + outlined_volume, RoomsVolume(world));
}

INSTANTIATE_TEST_SUITE_P(
    Scenes, ShippedRoomsTest,
    ::testing::ValuesIn(z13::testing::ShippedScenes()),
    [](const ::testing::TestParamInfo<std::string>& info) { return info.param; });

constexpr std::string_view kHubScene = "hub";
const Eigen::Vector3i kSpineEndCell {338, 413, 6};
const Eigen::Vector3i kFreightLobbyCell {348, 414, 6};
const Eigen::Vector3i kFreightHangarCell {420, 425, 12};

bool Joined(const RoomGraph& graph, std::optional<rooms::RoomId> a, std::optional<rooms::RoomId> b) {
  return std::ranges::any_of(graph.portals, [&](const rooms::Portal& portal) {
    return (portal.a == a && portal.b == b) || (portal.a == b && portal.b == a);
  });
}

TEST(HubFreightHangarTest, TheHangarIsASealedRoomBehindALobbyJoinedToTheSpineByDoors) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();

  const RoomGraph& graph = Graph(world);
  const auto spine = graph.RoomAt(kSpineEndCell);
  const auto lobby = graph.RoomAt(kFreightLobbyCell);
  const auto hangar = graph.RoomAt(kFreightHangarCell);
  ASSERT_TRUE(spine.has_value() && *spine != rooms::kVacuumRoom) << "spine";
  ASSERT_TRUE(lobby.has_value() && *lobby != rooms::kVacuumRoom) << "lobby";
  ASSERT_TRUE(hangar.has_value() && *hangar != rooms::kVacuumRoom) << "hangar";
  EXPECT_NE(spine, lobby);
  EXPECT_NE(lobby, hangar);
  EXPECT_TRUE(Joined(graph, spine, lobby));
  EXPECT_TRUE(Joined(graph, lobby, hangar));
}

const Eigen::Vector3i kAtriumDoorCell {231, 254, 6};
const Eigen::Vector3i kArrivalsHallCell {304, 170, 12};

TEST(HubArrivalsHangarTest, TheHallIsASealedRoomJoinedToTheAtriumByADoor) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();

  const RoomGraph& graph = Graph(world);
  const auto atrium = graph.RoomAt(kAtriumDoorCell);
  const auto hall = graph.RoomAt(kArrivalsHallCell);
  ASSERT_TRUE(atrium.has_value() && *atrium != rooms::kVacuumRoom);
  ASSERT_TRUE(hall.has_value() && *hall != rooms::kVacuumRoom);
  EXPECT_NE(atrium, hall);
  EXPECT_TRUE(std::ranges::any_of(graph.portals, [&](const rooms::Portal& portal) {
    return (portal.a == atrium && portal.b == hall) || (portal.a == hall && portal.b == atrium);
  }));
}

const Eigen::Vector3i kWestCorridorCell {8, 304, 6};
const Eigen::Vector3i kOldTunnelCell {-30, 297, 6};
const Eigen::Vector3i kOldHallCell {-135, 280, 12};

TEST(HubOldWingTest, TheTunnelAndTheHallAreOneSealedRoomJoinedToTheCorridorByADoor) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();

  const RoomGraph& graph = Graph(world);
  const auto corridor = graph.RoomAt(kWestCorridorCell);
  const auto tunnel = graph.RoomAt(kOldTunnelCell);
  const auto hall = graph.RoomAt(kOldHallCell);
  ASSERT_TRUE(corridor.has_value() && *corridor != rooms::kVacuumRoom) << "corridor";
  ASSERT_TRUE(tunnel.has_value() && *tunnel != rooms::kVacuumRoom) << "tunnel";
  ASSERT_TRUE(hall.has_value() && *hall != rooms::kVacuumRoom) << "hall";
  EXPECT_NE(corridor, tunnel);
  EXPECT_EQ(tunnel, hall);
  EXPECT_TRUE(Joined(graph, corridor, hall));
}

TEST(HubDoorsTest, EveryDoorStandsOpenAtTheStart) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();

  int doors = 0;
  int shut = 0;
  world.World().each([&](const DoorState& state) {
    ++doors;
    shut += state.position != kDoorOpen || state.leaf_blocks;
  });
  EXPECT_GT(doors, 0);
  EXPECT_EQ(shut, 0);
}

}  // namespace
}  // namespace z13::station
