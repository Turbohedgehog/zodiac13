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
#include <vector>

#include <Eigen/Dense>

#include <rooms/light_reach.h>
#include <rooms/room_builder.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;
using z13::building::primitives::PointLight;

// Two rooms along x, A [1, 5) and B [6, 11), 4 wide and 4 high, behind the wall at x = 5.
constexpr int kLength = 10;
constexpr int kSide = 4;
constexpr int kHeight = 4;
constexpr int kWall = 5;
constexpr int kRadius = 20;

// `portals`: openings in the walls, e.g. a window between the rooms.
RoomGraph TwoRooms(const std::vector<PortalSource>& portals = {}) {
  const Eigen::Vector3i outer(kLength + 2, kSide + 2, kHeight + 2);
  const std::vector<CellBox> sealed {
      {.min = {0, 0, 0}, .extent = {outer.x(), outer.y(), 1}},
      {.min = {0, 0, outer.z() - 1}, .extent = {outer.x(), outer.y(), 1}},
      {.min = {0, 0, 1}, .extent = {1, outer.y(), kHeight}},
      {.min = {outer.x() - 1, 0, 1}, .extent = {1, outer.y(), kHeight}},
      {.min = {1, 0, 1}, .extent = {kLength, 1, kHeight}},
      {.min = {1, outer.y() - 1, 1}, .extent = {kLength, 1, kHeight}},
      {.min = {kWall, 1, 1}, .extent = {1, kSide, kHeight}},
  };
  return BuildRooms({.sealed = sealed, .portals = portals}).value();
}

PointLight LightAt(const Eigen::Vector3f& position, int radius = kRadius) {
  return {.position = position, .cell = position.array().floor().cast<int>(), .source = {.radius_cells = radius}};
}

const Eigen::Vector3f kInA(2.5f, 2.5f, 4.5f);
const Eigen::Vector3i kInB(8, 2, 2);
const PortalSource kWindow {.opening = {.min = {kWall, 2, 2}, .extent = {1, 2, 2}}, .axis = Axis::kX, .visible = true};

TEST(LightReachTest, ALightStopsAtItsRoomsWalls) {
  const RoomGraph graph = TwoRooms();

  const LightReach light = ReachOf(LightAt(kInA), graph).value();

  ASSERT_TRUE(light.room.has_value());
  EXPECT_EQ(light.room, graph.RoomAt(kInA.cast<int>()));
  EXPECT_EQ(light.lit_rooms, std::vector {*light.room});
  EXPECT_TRUE(light.reach.min().isApprox(Eigen::Vector3f::Zero()));
  EXPECT_TRUE(light.reach.max().isApprox(Eigen::Vector3f(kWall + 1, kSide + 2, kHeight + 2)));
}

TEST(LightReachTest, ALightShinesThroughAWindowIntoTheNextRoom) {
  const RoomGraph graph = TwoRooms({kWindow});

  const LightReach light = ReachOf(LightAt(kInA), graph).value();

  ASSERT_TRUE(light.room.has_value());
  const auto next = graph.RoomAt(kInB);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(light.lit_rooms, std::vector({std::min(*light.room, *next), std::max(*light.room, *next)}));
  // Its shadow lets it through the window; without one it stays in its room.
  EXPECT_TRUE(light.reach.max().isApprox(Eigen::Vector3f(kWall + 1, kSide + 2, kHeight + 2)));
}

TEST(LightReachTest, AWindowBeyondTheRadiusLetsNoLightThrough) {
  constexpr int kShortRadius = 2;
  const RoomGraph graph = TwoRooms({kWindow});

  const LightReach light = ReachOf(LightAt(kInA, kShortRadius), graph).value();

  EXPECT_EQ(light.lit_rooms.size(), 1U);
}

TEST(LightReachTest, ALightOutsideOrWithoutRoomsReachesItsRadius) {
  const RoomGraph graph = TwoRooms();
  const Eigen::Vector3f outside(-5.f, 2.f, 2.f);
  const Eigen::Vector3f radius = Eigen::Vector3f::Constant(kRadius);

  const LightReach in_space = ReachOf(LightAt(outside), graph).value();
  EXPECT_FALSE(in_space.room.has_value());
  EXPECT_TRUE(in_space.reach.isApprox(Eigen::AlignedBox3f(outside - radius, outside + radius)));
  EXPECT_TRUE(ReachOf(LightAt(kInA), std::nullopt)->reach.isApprox(Eigen::AlignedBox3f(kInA - radius, kInA + radius)));
}

TEST(LightReachTest, ALightInsideABlockLightsNothing) {
  const RoomGraph graph = TwoRooms();
  const Eigen::Vector3f in_the_wall(kWall + 0.5f, 2.5f, 2.5f);

  EXPECT_FALSE(ReachOf(LightAt(in_the_wall), graph).has_value());
}

TEST(LightReachTest, TheRadiusBoxIgnoresTheWalls) {
  const RoomGraph graph = TwoRooms();
  const Eigen::Vector3f radius = Eigen::Vector3f::Constant(kRadius);

  EXPECT_TRUE(ReachOf(LightAt(kInA), graph)->radius_box.isApprox(Eigen::AlignedBox3f(kInA - radius, kInA + radius)));
}

}  // namespace
}  // namespace z13::station::rooms
