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

#include <z13/components/station/dock.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/door.h>
#include <z13/components/station/rooms.h>
#include <z13/components/station/block.h>
#include <z13/components/station/spawn_point.h>
#include <z13/components/utility/utilities.h>
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
  return MainGridEntity(world.World()).get<RoomCache>().Current()->get();
}

uint64_t Version(Z13TestWorld& world) {
  return MainGridEntity(world.World()).get<TopologyVersion>().version;
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
    const uint64_t fingerprint = MainGridEntity(world.World()).get<RoomCache>().CurrentFingerprint().value_or(0) + fingerprint_offset;
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

  const auto current = MainGridEntity(world.World()).get<RoomCache>().Current();
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

using z13::testing::kHubScene;
constexpr int kPowerSettleTicks = 30;  // a few power steps (PowerTuning::step_ticks)
const Eigen::Vector3i kSpineEndCell {338, 413, 6};
const Eigen::Vector3i kFreightLobbyCell {348, 414, 6};
constexpr z13::station::GridId kFreightGrid = 1;
// In the hangar grid's own cells.
const Eigen::Vector3i kFreightHangarCell {60, 48, 12};

std::optional<z13::station::DockLink> DockLinkTo(Z13TestWorld& world, GridId grid) {
  const auto& links = world.World().get<DockLinks>().list;
  const auto found = std::ranges::find_if(links, [grid](const auto& link) { return link.b.grid == grid; });
  return found == links.end() ? std::nullopt : std::optional(*found);
}

bool Joined(const RoomGraph& graph, std::optional<rooms::RoomId> a, std::optional<rooms::RoomId> b) {
  return std::ranges::any_of(graph.portals, [&](const rooms::Portal& portal) {
    return (portal.a == a && portal.b == b) || (portal.a == b && portal.b == a);
  });
}

TEST(HubFreightHangarTest, TheHangarIsAGridWithASealedRoomBehindALobbyJoinedToTheSpineByADoor) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();

  const RoomGraph& graph = Graph(world);
  const auto spine = graph.RoomAt(kSpineEndCell);
  const auto lobby = graph.RoomAt(kFreightLobbyCell);
  ASSERT_TRUE(spine.has_value() && *spine != rooms::kVacuumRoom) << "spine";
  ASSERT_TRUE(lobby.has_value() && *lobby != rooms::kVacuumRoom) << "lobby";
  EXPECT_NE(spine, lobby);
  EXPECT_TRUE(Joined(graph, spine, lobby));

  const flecs::entity hangar_grid = world.World().lookup(std::format("Grid_{}", kFreightGrid).c_str());
  ASSERT_TRUE(hangar_grid.is_valid() && hangar_grid.has<RoomCache>());
  const RoomGraph& hangar_graph = hangar_grid.get<RoomCache>().Current()->get();
  const auto hangar = hangar_graph.RoomAt(kFreightHangarCell);
  ASSERT_TRUE(hangar.has_value() && *hangar != rooms::kVacuumRoom) << "hangar";
  EXPECT_TRUE(Joined(hangar_graph, hangar, rooms::kVacuumRoom)) << "the hangar's door";

  const auto link = DockLinkTo(world, kFreightGrid);
  ASSERT_TRUE(link.has_value()) << "the hangar's dock";
  EXPECT_EQ(link->a, (RoomRef {.grid = z13::station::kMainGrid, .room = *lobby}));
  EXPECT_EQ(link->b, (RoomRef {.grid = kFreightGrid, .room = *hangar}));
}

const Eigen::Vector3i kAtriumDoorCell {231, 254, 6};
constexpr z13::station::GridId kArrivalsGrid = 2;
// In the arrivals grid's own cells.
const Eigen::Vector3i kArrivalsHallCell {100, 16, 12};

TEST(HubArrivalsHangarTest, TheHallIsAGridWithASealedRoomJoinedToTheAtriumByADock) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();

  const auto atrium = Graph(world).RoomAt(kAtriumDoorCell);
  ASSERT_TRUE(atrium.has_value() && *atrium != rooms::kVacuumRoom) << "atrium";

  const flecs::entity hall_grid = world.World().lookup(std::format("Grid_{}", kArrivalsGrid).c_str());
  ASSERT_TRUE(hall_grid.is_valid() && hall_grid.has<RoomCache>());
  const RoomGraph& hall_graph = hall_grid.get<RoomCache>().Current()->get();
  const auto hall = hall_graph.RoomAt(kArrivalsHallCell);
  ASSERT_TRUE(hall.has_value() && *hall != rooms::kVacuumRoom) << "hall";
  EXPECT_TRUE(Joined(hall_graph, hall, rooms::kVacuumRoom)) << "the hall's door";

  const auto link = DockLinkTo(world, kArrivalsGrid);
  ASSERT_TRUE(link.has_value()) << "the hall's dock";
  EXPECT_EQ(link->a, (RoomRef {.grid = z13::station::kMainGrid, .room = *atrium}));
  EXPECT_EQ(link->b, (RoomRef {.grid = kArrivalsGrid, .room = *hall}));
}

const Eigen::Vector3i kWestCorridorCell {8, 304, 6};
const Eigen::Vector3i kOldLobbyCell {-3, 302, 6};
constexpr z13::station::GridId kOldWingGrid = 3;
// In the old wing's grid, in its own cells.
const Eigen::Vector3i kOldTunnelCell {30, 12, 12};
const Eigen::Vector3i kOldHallCell {150, 12, 12};

TEST(HubOldWingTest, TheTunnelAndTheHallAreOneSealedRoomOfAGridBehindALobbyJoinedToTheCorridorByADoor) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();
  world.Tick();  // the doors get their state in the first tick, after the docks were linked

  const RoomGraph& graph = Graph(world);
  const auto corridor = graph.RoomAt(kWestCorridorCell);
  const auto lobby = graph.RoomAt(kOldLobbyCell);
  ASSERT_TRUE(corridor.has_value() && *corridor != rooms::kVacuumRoom) << "corridor";
  ASSERT_TRUE(lobby.has_value() && *lobby != rooms::kVacuumRoom) << "lobby";
  EXPECT_NE(corridor, lobby);
  EXPECT_TRUE(Joined(graph, corridor, lobby));

  const flecs::entity wing_grid = world.World().lookup(std::format("Grid_{}", kOldWingGrid).c_str());
  ASSERT_TRUE(wing_grid.is_valid() && wing_grid.has<RoomCache>());
  const RoomGraph& wing_graph = wing_grid.get<RoomCache>().Current()->get();
  const auto tunnel = wing_graph.RoomAt(kOldTunnelCell);
  const auto hall = wing_graph.RoomAt(kOldHallCell);
  ASSERT_TRUE(tunnel.has_value() && *tunnel != rooms::kVacuumRoom) << "tunnel";
  ASSERT_TRUE(hall.has_value() && *hall != rooms::kVacuumRoom) << "hall";
  EXPECT_EQ(tunnel, hall);
  EXPECT_TRUE(Joined(wing_graph, tunnel, rooms::kVacuumRoom)) << "the near wall's door";

  const auto link = DockLinkTo(world, kOldWingGrid);
  ASSERT_TRUE(link.has_value()) << "the wing's dock";
  EXPECT_EQ(link->a, (RoomRef {.grid = z13::station::kMainGrid, .room = *lobby}));
  EXPECT_EQ(link->b, (RoomRef {.grid = kOldWingGrid, .room = *tunnel}));
  // The dock's door is the near wall's, west of the corridor's end wall whose own door stands nearly as close.
  ASSERT_NE(link->door_a.door, 0u);
  EXPECT_LT(world.World().entity(link->door_a.door).get<Block>().cell.x(), 0);
}

// The reactor shaft: the second deck's galleries and bridge are cells of the tunnel's own room, over the core.
const Eigen::Vector3i kSouthGalleryCell {50, 3, 13};
const Eigen::Vector3i kNorthGalleryCell {50, 20, 13};
const Eigen::Vector3i kBridgeCell {69, 12, 13};
constexpr std::string_view kReactor = "Reactor";

TEST(HubOldWingTest, TheReactorStandsInTheTunnelBelowGalleriesOfItsRoom) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();

  size_t reactors {};
  world.World().query_builder<const Block>().build().each([&](const Block& placed) {
    reactors += placed.grid == kOldWingGrid && placed.spec.type_id == IdOf(kReactor) ? 1 : 0;
  });
  EXPECT_EQ(reactors, 1u);

  const flecs::entity wing_grid = world.World().lookup(std::format("Grid_{}", kOldWingGrid).c_str());
  ASSERT_TRUE(wing_grid.is_valid() && wing_grid.has<RoomCache>());
  const RoomGraph& graph = wing_grid.get<RoomCache>().Current()->get();
  const auto tunnel = graph.RoomAt(kOldTunnelCell);
  ASSERT_TRUE(tunnel.has_value() && *tunnel != rooms::kVacuumRoom) << "tunnel";
  for (const Eigen::Vector3i& cell : {kSouthGalleryCell, kNorthGalleryCell, kBridgeCell}) {
    EXPECT_EQ(graph.RoomAt(cell), tunnel) << cell.transpose();
    bool floor_below = false;
    world.World().query_builder<const Block>().build().each([&](const Block& placed) {
      const Eigen::Vector3i below = cell - Eigen::Vector3i::UnitZ();
      floor_below |= placed.grid == kOldWingGrid && placed.spec.type_id == IdOf(kFloor) &&
                     (below.array() >= placed.cell.array()).all() &&
                     (below.array() < (placed.cell + placed.spec.size).array()).all();
    });
    EXPECT_TRUE(floor_below) << "a slab under " << cell.transpose();
  }
}

// The tech trunk is a corridor a deck under the spine; the flight down from the spine corridor opens its ceiling.
const Eigen::Vector3i kTrunkWestCell {20, 294, -6};
const Eigen::Vector3i kTrunkEastCell {104, 294, -6};
const Eigen::Vector3i kSpineCorridorCell {20, 308, 6};

TEST(HubTechTrunkTest, TheTrunkBelowTheSpineIsOneRoomWithTheCorridorAboveItThroughTheFlightsOpening) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();

  const RoomGraph& graph = Graph(world);
  const auto west = graph.RoomAt(kTrunkWestCell);
  ASSERT_TRUE(west.has_value() && *west != rooms::kVacuumRoom) << "the trunk's west end";
  EXPECT_EQ(graph.RoomAt(kTrunkEastCell), west) << "the trunk's two sides of the flight";
  EXPECT_EQ(graph.RoomAt(kSpineCorridorCell), west) << "the spine corridor";
}

// The halls of equipment either side of the trunk, each a room of its own behind a door in the trunk's wall.
const Eigen::Vector3i kAtmosHallCell {60, 276, -6};
const Eigen::Vector3i kPowerHallCell {60, 312, -6};

TEST(HubTechTrunkTest, TheAtmosphereAndPowerHallsAreRoomsBehindDoorsInTheTrunksWalls) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();

  const RoomGraph& graph = Graph(world);
  const auto trunk = graph.RoomAt(kTrunkWestCell);
  ASSERT_TRUE(trunk.has_value() && *trunk != rooms::kVacuumRoom) << "trunk";
  for (const Eigen::Vector3i& cell : {kAtmosHallCell, kPowerHallCell}) {
    const auto hall = graph.RoomAt(cell);
    ASSERT_TRUE(hall.has_value() && *hall != rooms::kVacuumRoom) << cell.transpose();
    EXPECT_NE(hall, trunk) << cell.transpose();
    EXPECT_TRUE(Joined(graph, trunk, hall)) << "the door " << cell.transpose();
  }
}

// The trunk's lobby under the old wing's, joined to the trunk by a door and by a dock to the wing's lower deck, which a
// flight joins to the reactor's tunnel.
const Eigen::Vector3i kTrunkLobbyCell {-1, 296, -6};
const Eigen::Vector3i kWingLowerDeckCell {10, 12, -6};

TEST(HubTechTrunkTest, TheTrunkReachesTheReactorsTunnelThroughALobbyADockAndAFlight) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();
  world.Tick();  // the doors get their state in the first tick, after the docks were linked

  const RoomGraph& graph = Graph(world);
  const auto trunk = graph.RoomAt(kTrunkWestCell);
  const auto lobby = graph.RoomAt(kTrunkLobbyCell);
  ASSERT_TRUE(trunk.has_value() && *trunk != rooms::kVacuumRoom) << "trunk";
  ASSERT_TRUE(lobby.has_value() && *lobby != rooms::kVacuumRoom) << "lobby";
  EXPECT_NE(trunk, lobby);
  EXPECT_TRUE(Joined(graph, trunk, lobby)) << "the trunk's end door";

  const flecs::entity wing_grid = world.World().lookup(std::format("Grid_{}", kOldWingGrid).c_str());
  ASSERT_TRUE(wing_grid.is_valid() && wing_grid.has<RoomCache>());
  const RoomGraph& wing_graph = wing_grid.get<RoomCache>().Current()->get();
  const auto lower = wing_graph.RoomAt(kWingLowerDeckCell);
  ASSERT_TRUE(lower.has_value() && *lower != rooms::kVacuumRoom) << "the wing's lower deck";
  EXPECT_EQ(wing_graph.RoomAt(kOldTunnelCell), lower) << "the flight opens the tunnel's floor";

  const auto& links = world.World().get<DockLinks>().list;
  const RoomRef lobby_ref {.grid = z13::station::kMainGrid, .room = *lobby};
  const auto link = std::ranges::find_if(links, [&](const auto& found) { return found.a == lobby_ref; });
  ASSERT_NE(link, links.end()) << "the dock";
  EXPECT_EQ(link->b, (RoomRef {.grid = kOldWingGrid, .room = *lower}));
  EXPECT_NE(link->door_a.door, 0u) << "the near wall's door a deck down";
  EXPECT_NE(link->door_b.door, 0u);
}

// Each district's lower deck: a corridor under its hall, one room with the hall through the flight's opening in the floor.
struct DistrictTechCase {
  z13::station::GridId grid {};
  Eigen::Vector3i hall;
  Eigen::Vector3i lower_west;
  Eigen::Vector3i lower_east;
};

class HubDistrictTechTest : public ::testing::TestWithParam<DistrictTechCase> {};

TEST_P(HubDistrictTechTest, TheLowerDeckIsOneRoomWithTheHallOnBothSidesOfTheFlight) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();

  const DistrictTechCase& district = GetParam();
  const flecs::entity grid = world.World().lookup(std::format("Grid_{}", district.grid).c_str());
  ASSERT_TRUE(grid.is_valid() && grid.has<RoomCache>());
  const RoomGraph& graph = grid.get<RoomCache>().Current()->get();
  const auto hall = graph.RoomAt(district.hall);
  ASSERT_TRUE(hall.has_value() && *hall != rooms::kVacuumRoom) << "hall";
  EXPECT_EQ(graph.RoomAt(district.lower_west), hall) << "west of the flight";
  EXPECT_EQ(graph.RoomAt(district.lower_east), hall) << "east of the flight";
}

INSTANTIATE_TEST_SUITE_P(
    Districts, HubDistrictTechTest,
    ::testing::Values(DistrictTechCase {1, {60, 48, 12}, {38, 48, -6}, {84, 48, -6}},
                      DistrictTechCase {2, {100, 16, 12}, {93, 16, -6}, {139, 16, -6}},
                      DistrictTechCase {4, {60, 48, 12}, {48, 48, -6}, {94, 48, -6}}));

// The mining district's hall is a grid south of the departure hangar, behind a lobby wedge joined to the hangar by a door.
constexpr int kMiningGrid = 4;
constexpr int kHangarWallCellY = 16;
const Eigen::Vector3i kHangarCell {184, 70, 6};
const Eigen::Vector3i kMiningLobbyCell {184, 8, 6};
const Eigen::Vector3i kMiningHallCell {60, 48, 12};

TEST(HubMiningTest, TheHallIsASealedRoomOfAGridBehindALobbyJoinedToTheHangarByADoor) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  world.Tick();
  world.Tick();  // the doors get their state in the first tick, after the docks were linked

  const RoomGraph& graph = Graph(world);
  const auto hangar = graph.RoomAt(kHangarCell);
  const auto lobby = graph.RoomAt(kMiningLobbyCell);
  ASSERT_TRUE(hangar.has_value() && *hangar != rooms::kVacuumRoom) << "hangar";
  ASSERT_TRUE(lobby.has_value() && *lobby != rooms::kVacuumRoom) << "lobby";
  EXPECT_NE(hangar, lobby);
  EXPECT_TRUE(Joined(graph, hangar, lobby));

  const flecs::entity district = world.World().lookup(std::format("Grid_{}", kMiningGrid).c_str());
  ASSERT_TRUE(district.is_valid() && district.has<RoomCache>());
  const RoomGraph& district_graph = district.get<RoomCache>().Current()->get();
  const auto hall = district_graph.RoomAt(kMiningHallCell);
  ASSERT_TRUE(hall.has_value() && *hall != rooms::kVacuumRoom) << "hall";
  EXPECT_TRUE(Joined(district_graph, hall, rooms::kVacuumRoom)) << "the near wall's door";

  const auto link = DockLinkTo(world, kMiningGrid);
  ASSERT_TRUE(link.has_value()) << "the district's dock";
  EXPECT_EQ(link->a, (RoomRef {.grid = z13::station::kMainGrid, .room = *lobby}));
  EXPECT_EQ(link->b, (RoomRef {.grid = kMiningGrid, .room = *hall}));
  // The dock's door is the near wall's, south of the hangar's wall whose own door stands nearly as close.
  ASSERT_NE(link->door_a.door, 0u);
  EXPECT_LT(world.World().entity(link->door_a.door).get<Block>().cell.y(), kHangarWallCellY);
}

// The hub's cables power its doors, so they work by themselves: shut until a body comes (the blueprint no longer
// starts them open for want of power).
TEST(HubDoorsTest, EveryDoorIsPoweredAndShutUntilABodyComes) {
  Z13TestWorld world({std::string(z13::testing::kStationSceneArg), std::string(kHubScene)});
  for (int tick = 0; tick < kPowerSettleTicks; ++tick) {
    world.Tick();
  }

  int doors = 0;
  int unpowered = 0;
  int open = 0;
  world.World().each([&](flecs::entity door, const DoorState& state) {
    ++doors;
    unpowered += !z13::utility::IsSupplied(door);
    open += state.position != 0 || state.open_wanted;
  });
  EXPECT_GT(doors, 0);
  EXPECT_EQ(unpowered, 0);
  EXPECT_EQ(open, 0);
}

// What is derived from a grid's blocks is a component of its entity, and each grid has its own.
class GridRoomsTest : public ::testing::Test {
 protected:
  static constexpr GridId kSecondGrid = 1;
  // The second grid gets its rooms on the first tick that sees it; a few more to read them.
  static constexpr int kSettleTicks = 3;
  // Clear of the main grid's box, so the blocks' names differ.
  static inline const Eigen::Vector3i kShift {200, 0, 0};

  void SetUp() override {
    world_.Tick();
    AddBlocks(world_, SealedBox());
    second_ = world_.World().entity("SecondGrid").add<z13::flecs_tools::StateEntity>().set(Grid {.id = kSecondGrid});
    Advance(kSettleTicks);
  }

  void Advance(int ticks) {
    for (int i = 0; i < ticks; ++i) {
      world_.Tick();
    }
  }

  // The sealed box again, in the second grid.
  void BuildSecondBox() {
    for (Block block : SealedBox()) {
      block.cell += kShift;
      block.grid = kSecondGrid;
      AddBlock(world_, block);
    }
    Advance(kSettleTicks);
  }

  const RoomGraph& SecondGraph() { return second_.get<RoomCache>().Current()->get(); }

  Z13TestWorld world_ = StationWorld();
  flecs::entity second_;
};

TEST_F(GridRoomsTest, ABoxInAnotherGridMakesRoomsThereAndLeavesTheMainGridsAlone) {
  const uint64_t version = Version(world_);
  const uint64_t fingerprint = MainGridEntity(world_.World()).get<TopologyVersion>().fingerprint;
  const size_t main_rooms = Graph(world_).rooms.size();

  BuildSecondBox();

  const auto inside = SecondGraph().RoomAt(kRoomCorner + kShift + Eigen::Vector3i(1, 1, 1));
  ASSERT_TRUE(inside.has_value());
  EXPECT_EQ(SecondGraph().rooms.at(*inside).volume_cells, kSide * kSide * kHeight);
  EXPECT_EQ(Version(world_), version);
  EXPECT_EQ(MainGridEntity(world_.World()).get<TopologyVersion>().fingerprint, fingerprint);
  EXPECT_EQ(Graph(world_).rooms.size(), main_rooms);
}

TEST_F(GridRoomsTest, EditingTheMainGridLeavesTheOtherGridsTopologyAlone) {
  BuildSecondBox();
  const uint64_t second_version = second_.get<TopologyVersion>().version;
  const uint64_t main_version = Version(world_);

  AddBlocks(world_, DoorPartition());
  world_.Tick();

  EXPECT_GT(Version(world_), main_version);
  EXPECT_EQ(second_.get<TopologyVersion>().version, second_version);
}

// A dock joins the rooms holding its two cells, in whichever grids they are.
TEST_F(GridRoomsTest, ADockJoinsTheRoomsOfTwoGrids) {
  BuildSecondBox();
  const Eigen::Vector3i inside = kRoomCorner + Eigen::Vector3i(1, 1, 1);
  world_.World()
      .entity("Dock")
      .add<z13::flecs_tools::StateEntity>()
      .set(Dock {.a = {.grid = kMainGrid, .cell = inside}, .b = {.grid = kSecondGrid, .cell = inside + kShift}});
  Advance(1);

  const auto& links = world_.World().get<DockLinks>().list;
  ASSERT_EQ(links.size(), 1u);
  EXPECT_EQ(links[0].a, (RoomRef {.grid = kMainGrid, .room = *Graph(world_).RoomAt(inside)}));
  EXPECT_EQ(links[0].b, (RoomRef {.grid = kSecondGrid, .room = *SecondGraph().RoomAt(inside + kShift)}));
}

TEST_F(GridRoomsTest, ADockWithAnEndInVacuumJoinsNothing) {
  BuildSecondBox();
  const Eigen::Vector3i inside = kRoomCorner + Eigen::Vector3i(1, 1, 1);
  const Eigen::Vector3i outside = kRoomCorner + kShift - Eigen::Vector3i(10, 10, 10);
  world_.World()
      .entity("Dock")
      .add<z13::flecs_tools::StateEntity>()
      .set(Dock {.a = {.grid = kMainGrid, .cell = inside}, .b = {.grid = kSecondGrid, .cell = outside}});
  Advance(1);

  EXPECT_TRUE(world_.World().get<DockLinks>().list.empty());
}

flecs::entity DoorOfGrid(Z13TestWorld& world, GridId grid) {
  flecs::entity found;
  world.World().query_builder<const Block>().build().each([&](flecs::entity block, const Block& placed) {
    if (placed.grid == grid && placed.spec.type_id == IdOf(kDoor)) {
      found = block;
    }
  });
  return found;
}

// Both boxes split by a partition with a door, docked through the rooms beside the doors.
class DockedDoorsTest : public GridRoomsTest {
 protected:
  // Long enough for a door to start moving.
  static constexpr int kOpenTicks = 10;

  void SetUp() override {
    GridRoomsTest::SetUp();
    AddBlocks(world_, DoorPartition());
    for (Block block : SealedBox()) {
      block.cell += kShift;
      block.grid = kSecondGrid;
      AddBlock(world_, block);
    }
    for (Block block : DoorPartition()) {
      block.cell += kShift;
      block.grid = kSecondGrid;
      AddBlock(world_, block);
    }
    const Eigen::Vector3i beside_door = kRoomCorner + Eigen::Vector3i(3, 3, 3);
    world_.World()
        .entity("Dock")
        .add<z13::flecs_tools::StateEntity>()
        .set(Dock {.a = {.grid = kMainGrid, .cell = beside_door}, .b = {.grid = kSecondGrid, .cell = beside_door + kShift}});
    Advance(kSettleTicks + 2);
  }
};

TEST_F(DockedDoorsTest, ADockNamesTheNearestDoorOfEachEndsRoomWithItsOpening) {
  const auto& links = world_.World().get<DockLinks>().list;
  ASSERT_EQ(links.size(), 1u);

  EXPECT_EQ(links[0].door_a.door, DoorOfGrid(world_, kMainGrid).id());
  EXPECT_EQ(links[0].door_b.door, DoorOfGrid(world_, kSecondGrid).id());
  EXPECT_GT(links[0].door_a.area_cells, 0);
  EXPECT_GT(links[0].door_b.area_cells, 0);
}

TEST_F(DockedDoorsTest, AWishToOpenOneDoorOfThePairOpensTheOtherToo) {
  const flecs::entity a = DoorOfGrid(world_, kMainGrid);
  const flecs::entity b = DoorOfGrid(world_, kSecondGrid);
  ASSERT_STRNE(a.name().c_str(), b.name().c_str());
  const bool a_leads = std::string_view(a.name().c_str()) < std::string_view(b.name().c_str());
  const flecs::entity lead = a_leads ? a : b;
  const flecs::entity follower = a_leads ? b : a;
  ASSERT_FALSE(follower.get<DoorState>().open_wanted);

  lead.set(DoorState {.mode = DoorMode::kManual, .open_wanted = true});
  Advance(kOpenTicks);

  EXPECT_TRUE(follower.get<DoorState>().open_wanted);
  EXPECT_EQ(lead.get<DoorState>().position, follower.get<DoorState>().position);
  EXPECT_GT(follower.get<DoorState>().position, 0);
}

}  // namespace
}  // namespace z13::station
