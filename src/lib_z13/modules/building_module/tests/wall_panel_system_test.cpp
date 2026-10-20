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
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>

#include <z13/components/building.h>
#include <z13/components/block.h>
#include <z13/components/gameplay.h>
#include <z13/components/wall.h>
#include <primitives/wall_panel.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_entities.h"
#include "support/station_builders.h"

namespace z13::building {
namespace {

using z13::station::Block;
using z13::station::WallAxis;
using z13::station::WallNode;
using z13::station::WallPanelOf;
using z13::station::WallSegment;
using z13::testing::AddBuilder;
using z13::testing::Facing;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr uint32_t kBuilderId = 7;
// Off the cell boundary at y = 0, so the destroy ray stays in the wall's row.
const Eigen::Vector3f kBuilderPosition {0.f, 0.1f, 1.25f};
constexpr int kStickLength = 8;
constexpr int kThickness = 2;

std::vector<Block> Panels(flecs::world world) {
  std::vector<Block> panels;
  world.query<const WallPanelOf, const Block>().each([&panels](const WallPanelOf&, const Block& block) {
    panels.push_back(block);
  });
  return panels;
}

// A wall across the builder's view, 8 cells out, 5 tall; returns its segment.
flecs::entity AddWall(Z13TestWorld& test_world, const Eigen::Vector3i& from, const Eigen::Vector3i& to) {
  flecs::world world = test_world.World();
  auto& counters = world.get_mut<z13::gameplay::IdCounters>();
  const uint32_t a = SpawnWallNode(world, counters, from).get<WallNode>().id;
  const uint32_t b = SpawnWallNode(world, counters, to).get<WallNode>().id;
  return SpawnWallSegment(
      world, counters,
      {.node_a = a, .node_b = b, .stick_axis = WallAxis::kZ, .stick_length = kStickLength, .thickness = kThickness});
}

Block ExpectedPanel(const Eigen::Vector3i& from, const Eigen::Vector3i& to) {
  return z13::building::primitives::WallPanelBlock(
             {.node_a = from, .node_b = to, .stick_axis = WallAxis::kZ, .stick_length = kStickLength, .thickness = kThickness},
             z13::testing::ShippedPalette().value())
      .value();
}

const Eigen::Vector3i kFrom {8, -4, 0};
const Eigen::Vector3i kTo {8, 4, 0};

TEST(WallPanelSystemTest, ASegmentGetsOnePanelBlock) {
  Z13TestWorld test_world = StationWorld();
  AddWall(test_world, kFrom, kTo);
  test_world.Tick();
  test_world.Tick();

  const std::vector<Block> panels = Panels(test_world.World());
  ASSERT_EQ(panels.size(), 1u);
  EXPECT_EQ(panels.front(), ExpectedPanel(kFrom, kTo));
}

TEST(WallPanelSystemTest, MovingANodeRebuildsThePanel) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity segment = AddWall(test_world, kFrom, kTo);
  test_world.Tick();
  const Eigen::Vector3i moved {8, 6, 0};

  FindWallNode(test_world.World(), segment.get<WallSegment>().node_b)->set(WallNode {.id = segment.get<WallSegment>().node_b, .cell = moved});
  test_world.Tick();
  test_world.Tick();

  const std::vector<Block> panels = Panels(test_world.World());
  ASSERT_EQ(panels.size(), 1u);
  EXPECT_EQ(panels.front(), ExpectedPanel(kFrom, moved));
}

TEST(WallPanelSystemTest, RemovingASegmentRemovesItsPanelAndItsNodes) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity segment = AddWall(test_world, kFrom, kTo);
  test_world.Tick();
  ASSERT_EQ(Panels(test_world.World()).size(), 1u);

  DestroyWallSegment(test_world.World(), segment.get<WallSegment>().id);
  test_world.Tick();
  test_world.Tick();

  EXPECT_TRUE(Panels(test_world.World()).empty());
  EXPECT_EQ(test_world.World().count<WallNode>(), 0);
}

TEST(WallPanelSystemTest, SegmentsSharingANodeKeepItWhenOneIsRemoved) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  auto& counters = world.get_mut<z13::gameplay::IdCounters>();
  const uint32_t shared = SpawnWallNode(world, counters, kFrom).get<WallNode>().id;
  const uint32_t left = SpawnWallNode(world, counters, kTo).get<WallNode>().id;
  const uint32_t right = SpawnWallNode(world, counters, {8, -12, 0}).get<WallNode>().id;
  const WallSegment base {.stick_axis = WallAxis::kZ, .stick_length = kStickLength, .thickness = kThickness};
  const uint32_t first =
      SpawnWallSegment(world, counters, {.node_a = shared, .node_b = left, .stick_axis = base.stick_axis,
                                         .stick_length = base.stick_length, .thickness = base.thickness})
          .get<WallSegment>().id;
  SpawnWallSegment(world, counters, {.node_a = shared, .node_b = right, .stick_axis = base.stick_axis,
                                     .stick_length = base.stick_length, .thickness = base.thickness});
  test_world.Tick();
  ASSERT_EQ(Panels(world).size(), 2u);

  DestroyWallSegment(world, first);
  test_world.Tick();
  test_world.Tick();

  EXPECT_EQ(Panels(world).size(), 1u);
  EXPECT_TRUE(FindWallNode(world, shared).has_value());
  EXPECT_FALSE(FindWallNode(world, left).has_value());
}

TEST(WallPanelSystemTest, ASaveAndLoadKeepsOnePanelPerSegment) {
  Z13TestWorld test_world = StationWorld();
  AddWall(test_world, kFrom, kTo);
  test_world.Tick();
  const auto snapshot = z13::flecs_tools::CaptureState(test_world.World());
  ASSERT_TRUE(snapshot.has_value()) << snapshot.error();

  Z13TestWorld loaded = StationWorld();
  ASSERT_TRUE(z13::flecs_tools::RestoreWorld(loaded.World(), *snapshot).has_value());
  loaded.Tick();
  loaded.Tick();

  EXPECT_EQ(Panels(loaded.World()), Panels(test_world.World()));
  EXPECT_EQ(loaded.World().count<WallSegment>(), 1);
}

TEST(WallPanelSystemTest, DestroyingAPanelRemovesItsSegment) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  AddWall(test_world, kFrom, kTo);
  test_world.Tick();
  ASSERT_EQ(Panels(test_world.World()).size(), 1u);

  builder.add<RequestDestroyBlock>();
  test_world.Tick();
  test_world.Tick();

  EXPECT_EQ(test_world.World().count<WallSegment>(), 0);
  EXPECT_TRUE(Panels(test_world.World()).empty());
}

TEST(WallPanelSystemTest, DestroyingAnObliquePanelRemovesItsSegment) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  AddWall(test_world, {8, -4, 0}, {12, 4, 0});
  test_world.Tick();
  ASSERT_EQ(Panels(test_world.World()).size(), 1u);

  builder.add<RequestDestroyBlock>();
  test_world.Tick();
  test_world.Tick();

  EXPECT_EQ(test_world.World().count<WallSegment>(), 0);
  EXPECT_TRUE(Panels(test_world.World()).empty());
}

TEST(WallPanelSystemTest, ARebuiltPanelCanBeDestroyedByTheRay) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  const flecs::entity segment = AddWall(test_world, {8, -4, 0}, {8, 4, 0});
  test_world.Tick();
  const uint32_t node_b = segment.get<WallSegment>().node_b;
  FindWallNode(test_world.World(), node_b)->set(WallNode {.id = node_b, .cell = {9, 4, 0}});
  test_world.Tick();
  test_world.Tick();
  ASSERT_EQ(Panels(test_world.World()).size(), 1u);

  builder.add<RequestDestroyBlock>();
  test_world.Tick();
  test_world.Tick();

  EXPECT_EQ(test_world.World().count<WallSegment>(), 0);
}

const Eigen::Vector3i kCornerFrom {8, -4, 0};
const Eigen::Vector3i kCorner {8, 4, 0};
const Eigen::Vector3i kCornerTo {14, 10, 0};

// Two walls from one node, the first along Y, the second leaving at an angle; returns the first.
flecs::entity AddCorner(Z13TestWorld& test_world) {
  flecs::world world = test_world.World();
  auto& counters = world.get_mut<z13::gameplay::IdCounters>();
  const auto node = [&](const Eigen::Vector3i& cell) { return SpawnWallNode(world, counters, cell).get<WallNode>().id; };
  const uint32_t from = node(kCornerFrom);
  const uint32_t corner = node(kCorner);
  const uint32_t to = node(kCornerTo);
  const auto wall = [&](uint32_t a, uint32_t b) {
    return SpawnWallSegment(world, counters, {.node_a = a, .node_b = b, .stick_axis = WallAxis::kZ,
                                              .stick_length = kStickLength, .thickness = kThickness});
  };
  const flecs::entity first = wall(from, corner);
  wall(corner, to);
  return first;
}

TEST(WallPanelSystemTest, TwoWallsAtANodeAreCutAlongOneSeam) {
  Z13TestWorld test_world = StationWorld();
  AddCorner(test_world);
  test_world.Tick();
  test_world.Tick();

  const std::vector<Block> panels = Panels(test_world.World());
  ASSERT_EQ(panels.size(), 2u);
  EXPECT_TRUE(panels[0].spec.HasCorners());
  EXPECT_TRUE(panels[1].spec.HasCorners());
}

TEST(WallPanelSystemTest, ADestroyedWallLeavesTheOtherSquareCutAgain) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity first = AddCorner(test_world);
  test_world.Tick();
  test_world.Tick();

  DestroyWallSegment(test_world.World(), first.get<WallSegment>().id);
  test_world.Tick();
  test_world.Tick();

  const std::vector<Block> panels = Panels(test_world.World());
  ASSERT_EQ(panels.size(), 1u);
  EXPECT_FALSE(panels.front().spec.HasCorners());
}

}  // namespace
}  // namespace z13::building
