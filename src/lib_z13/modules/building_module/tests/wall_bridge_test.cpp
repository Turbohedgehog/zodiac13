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

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/utils/math.h>
#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/wall.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_bridge.h"
#include "../src/wall_entities.h"
#include "support/station_builders.h"

// The Bridge mode: a wall from the stick at one node to the same stick at another.
namespace z13::building {
namespace {

using z13::station::BrushPreview;
using z13::station::WallAxis;
using z13::station::WallBridgeDraft;
using z13::station::WallNode;
using z13::station::WallSegment;
using z13::testing::AddBuilder;
using z13::testing::Facing;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr uint32_t kBuilderId = 7;
const Eigen::Vector3f kBuilderPosition {0.f, 0.1f, 1.25f};
constexpr int kThickness = 2;
constexpr int kStickLength = 8;
constexpr int kShortStick = 5;
constexpr int kRun = 10;
constexpr int kGap = 20;
const Eigen::Vector3f kEye {-50.f, 0.f, 0.f};

uint32_t AddNode(Z13TestWorld& test_world, const Eigen::Vector3i& cell) {
  flecs::world world = test_world.World();
  return SpawnWallNode(world, world.get_mut<z13::gameplay::IdCounters>(), cell).get<WallNode>().id;
}

void AddWall(Z13TestWorld& test_world, uint32_t a, uint32_t b, WallAxis axis, int stick_length) {
  flecs::world world = test_world.World();
  SpawnWallSegment(world, world.get_mut<z13::gameplay::IdCounters>(),
                   {.node_a = a, .node_b = b, .stick_axis = axis, .stick_length = stick_length, .thickness = kThickness});
}

// A wall of `stick_length` from `from` by `run`, so its node at `from` carries that stick.
uint32_t AddStickAt(
    Z13TestWorld& test_world, const Eigen::Vector3i& from, WallAxis axis, int stick_length,
    const Eigen::Vector3i& run = Eigen::Vector3i(0, kRun, 0)) {
  const uint32_t node = AddNode(test_world, from);
  AddWall(test_world, node, AddNode(test_world, from + run), axis, stick_length);
  return node;
}

std::expected<FacedWall, std::string> Bridge(Z13TestWorld& test_world, uint32_t first, uint32_t second) {
  return PlanBridge(test_world.World(), first, second, kThickness, kEye,
                    test_world.World().get<z13::building::primitives::BlockPalette>().palette);
}

TEST(WallBridgeTest, TwoNodesWithTheSameStickGetAWallBetweenThem) {
  Z13TestWorld test_world = StationWorld();
  const uint32_t a = AddStickAt(test_world, {0, 0, 0}, WallAxis::kZ, kStickLength);
  const uint32_t b = AddStickAt(test_world, {kGap, 0, 0}, WallAxis::kZ, kStickLength);

  const auto bridge = Bridge(test_world, a, b);

  ASSERT_TRUE(bridge.has_value()) << bridge.error();
  EXPECT_EQ(bridge->wall.node_a, Eigen::Vector3i(0, 0, 0));
  EXPECT_EQ(bridge->wall.node_b, Eigen::Vector3i(kGap, 0, 0));
  EXPECT_EQ(bridge->wall.stick_axis, WallAxis::kZ);
  EXPECT_EQ(bridge->wall.stick_length, kStickLength);
}

TEST(WallBridgeTest, SticksOfOtherAxisOrDirectionAreRefused) {
  Z13TestWorld test_world = StationWorld();
  const uint32_t a = AddStickAt(test_world, {0, 0, 0}, WallAxis::kZ, kStickLength);
  const uint32_t turned = AddStickAt(test_world, {0, 0, kGap}, WallAxis::kX, kStickLength);
  const uint32_t opposite = AddStickAt(test_world, {kGap, 0, kGap}, WallAxis::kZ, -kStickLength);

  EXPECT_FALSE(Bridge(test_world, a, turned).has_value());
  EXPECT_FALSE(Bridge(test_world, a, opposite).has_value());
}

TEST(WallBridgeTest, SticksOfOtherLengthMakeATrapezoidEitherWay) {
  Z13TestWorld test_world = StationWorld();
  const uint32_t tall = AddStickAt(test_world, {0, 0, 0}, WallAxis::kZ, kStickLength);
  const uint32_t shorter = AddStickAt(test_world, {kGap, 0, 0}, WallAxis::kZ, kShortStick);

  const auto down = Bridge(test_world, tall, shorter);
  const auto up = Bridge(test_world, shorter, tall);

  ASSERT_TRUE(down.has_value()) << down.error();
  ASSERT_TRUE(up.has_value()) << up.error();
  EXPECT_EQ(down->wall.stick_length, kStickLength);
  EXPECT_EQ(down->wall.stick_growth, kShortStick - kStickLength);
  EXPECT_EQ(up->wall.stick_length, kShortStick);
  EXPECT_EQ(up->wall.stick_growth, kStickLength - kShortStick);
  EXPECT_TRUE(down->block.spec.HasFaceCuts());
}

TEST(WallBridgeTest, ABridgeCarriesTheStickOfTheNodeItReaches) {
  Z13TestWorld test_world = StationWorld();
  const uint32_t a = AddStickAt(test_world, {0, 0, 0}, WallAxis::kZ, kStickLength);
  const uint32_t b = AddStickAt(test_world, {kGap, 0, 0}, WallAxis::kZ, kShortStick);
  const auto bridge = Bridge(test_world, a, b);
  ASSERT_TRUE(bridge.has_value()) << bridge.error();
  flecs::world world = test_world.World();
  SpawnWall(world, world.get_mut<z13::gameplay::IdCounters>(), bridge->wall);

  EXPECT_EQ(SticksAt(world, a), (std::vector<WallStick> {{WallAxis::kZ, kStickLength}, {WallAxis::kZ, kStickLength}}));
  EXPECT_EQ(SticksAt(world, b), (std::vector<WallStick> {{WallAxis::kZ, kShortStick}, {WallAxis::kZ, kShortStick}}));
}

TEST(WallBridgeTest, ANodeCannotBridgeToItself) {
  Z13TestWorld test_world = StationWorld();
  const uint32_t a = AddStickAt(test_world, {0, 0, 0}, WallAxis::kZ, kStickLength);

  EXPECT_FALSE(Bridge(test_world, a, a).has_value());
}

TEST(WallBridgeTest, ANodeWithoutAWallHasNoStick) {
  Z13TestWorld test_world = StationWorld();
  const uint32_t a = AddStickAt(test_world, {0, 0, 0}, WallAxis::kZ, kStickLength);
  const uint32_t bare = AddNode(test_world, {kGap, 0, 0});

  EXPECT_TRUE(SticksAt(test_world.World(), bare).empty());
  EXPECT_FALSE(Bridge(test_world, a, bare).has_value());
}

TEST(WallBridgeTest, TwoClicksLayAWallBetweenTheNodesUnderTheBrush) {
  Z13TestWorld test_world = StationWorld();
  const flecs::entity builder = AddBuilder(test_world, kBuilderId, Facing(kBuilderPosition, Eigen::Vector3f::UnitX()));
  z13::testing::BuildToolOf(builder).get_mut<BuildTool>().mode = ToolMode::kBridge;
  test_world.Tick();

  const auto brush_cell = [&] {
    std::optional<Eigen::Vector3i> cell;
    builder.children([&cell](flecs::entity child) {
      if (child.has<BrushPreview>()) {
        cell = child.get<BrushPreview>().block.cell;
      }
    });
    return cell.value();
  };
  // Two wall ends facing across a gap along Z, which the bridge closes.
  const uint32_t a = AddStickAt(test_world, brush_cell(), WallAxis::kX, kStickLength, {0, 0, -kRun});
  builder.add<RequestBuildBlock>();
  test_world.Tick();
  ASSERT_TRUE(builder.has<WallBridgeDraft>());
  EXPECT_EQ(builder.get<WallBridgeDraft>().node_id, a);

  Eigen::Matrix4f transform = builder.get<Eigen::Matrix4f>();
  z13::math::SetTranslation(
      Eigen::Vector3f(z13::math::ExtractTranslation<float>(transform) + Eigen::Vector3f(0.f, 0.f, 2.f)), transform);
  builder.set(transform);
  test_world.Tick();
  AddStickAt(test_world, brush_cell(), WallAxis::kX, kStickLength, {0, 0, kRun});
  const int walls_before = test_world.World().count<WallSegment>();
  builder.add<RequestBuildBlock>();
  test_world.Tick();

  EXPECT_FALSE(builder.has<WallBridgeDraft>());
  EXPECT_EQ(test_world.World().count<WallSegment>(), walls_before + 1);
}

}  // namespace
}  // namespace z13::building
