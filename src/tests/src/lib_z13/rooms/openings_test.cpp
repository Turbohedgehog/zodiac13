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

#include <rooms/room_builder.h>
#include <rooms/room_update.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;

constexpr int kSide = 10;
constexpr int kHeight = 6;
constexpr int kOuter = kSide + 2;
constexpr int kWallX = 5;
const Eigen::Vector3i kWest {2, 5, 3};
const Eigen::Vector3i kEast {8, 5, 3};

// Six slabs enclosing a kSide x kSide x kHeight room whose inside starts at (1, 1, 1).
std::vector<CellBox> Shell() {
  constexpr int tall = kHeight + 2;
  return {
      {.min = {0, 0, 0}, .extent = {kOuter, kOuter, 1}},
      {.min = {0, 0, tall - 1}, .extent = {kOuter, kOuter, 1}},
      {.min = {0, 0, 1}, .extent = {1, kOuter, kHeight}},
      {.min = {kOuter - 1, 0, 1}, .extent = {1, kOuter, kHeight}},
      {.min = {1, 0, 1}, .extent = {kSide, 1, kHeight}},
      {.min = {1, kOuter - 1, 1}, .extent = {kSide, 1, kHeight}},
  };
}

// The partition at x = kWallX with a hole `width` cells wide from y = 4 and `height` cells
// tall from the floor, held by the wall on both sides.
std::vector<CellBox> PartitionWithHole(int width, int height) {
  constexpr int hole_y = 4;
  return {
      {.min = {kWallX, 1, 1}, .extent = {1, hole_y - 1, kHeight}},
      {.min = {kWallX, hole_y + width, 1}, .extent = {1, kSide + 1 - hole_y - width, kHeight}},
      {.min = {kWallX, hole_y, 1 + height}, .extent = {1, width, kHeight - height}},
  };
}

std::vector<CellBox> With(std::vector<CellBox> boxes, const std::vector<CellBox>& more) {
  std::ranges::copy(more, std::back_inserter(boxes));
  return boxes;
}

RoomGraph Build(const RoomSources& sources) {
  auto graph = BuildRooms(sources);
  EXPECT_TRUE(graph.has_value()) << graph.error();
  return graph.value_or(RoomGraph {});
}

RoomId At(const RoomGraph& graph, const Eigen::Vector3i& cell) {
  return graph.RoomAt(cell).value_or(kVacuumRoom);
}

TEST(OpeningsTest, AHoleWhereADoorWasLeavesTwoRoomsAndAnOpenPortal) {
  const RoomGraph graph = Build({.sealed = With(Shell(), PartitionWithHole(2, 4))});

  ASSERT_NE(At(graph, kWest), At(graph, kEast));
  ASSERT_EQ(graph.openings.size(), 1U);
  EXPECT_EQ(graph.openings[0].opening, (CellBox {.min = {kWallX, 4, 1}, .extent = {1, 2, 4}}));
  ASSERT_EQ(graph.portals.size(), 1U);
  const Portal& portal = graph.portals[0];
  EXPECT_EQ(portal.area_cells, 2 * 4);
  EXPECT_TRUE(portal.visible);
  EXPECT_TRUE(portal.passable);
  EXPECT_EQ(portal.axis, Axis::kX);
  EXPECT_EQ(portal.low_side, At(graph, kWest));
  EXPECT_EQ(graph.RoomAt({kWallX, 4, 1}), std::nullopt);
}

TEST(OpeningsTest, AHoleWiderThanTheLimitJoinsTheRooms) {
  const RoomSources sources {.sealed = With(Shell(), PartitionWithHole(4, 4))};

  const auto graph = BuildRooms(sources, kVacuumRoom + 1, {.max_opening_cells = 3});

  ASSERT_TRUE(graph.has_value());
  EXPECT_EQ(At(*graph, kWest), At(*graph, kEast));
  EXPECT_TRUE(graph->openings.empty());
}

TEST(OpeningsTest, ACorridorOneCellWideIsOneRoom) {
  std::vector<CellBox> sealed = Shell();
  sealed.push_back({.min = {4, 1, 1}, .extent = {1, kSide, kHeight}});
  sealed.push_back({.min = {6, 1, 1}, .extent = {1, kSide, kHeight}});

  const RoomGraph graph = Build({.sealed = sealed});

  EXPECT_TRUE(graph.openings.empty());
  const RoomId corridor = At(graph, {5, 5, 3});
  EXPECT_EQ(graph.rooms.at(corridor).volume_cells, kSide * kHeight);
}

TEST(OpeningsTest, AGapBetweenAWallsEndAndTheShellIsNotAnOpening) {
  std::vector<CellBox> sealed = Shell();
  sealed.push_back({.min = {kWallX, 1, 1}, .extent = {1, kSide - 2, kHeight}});

  const RoomGraph graph = Build({.sealed = sealed});

  EXPECT_TRUE(graph.openings.empty());
  EXPECT_EQ(At(graph, kWest), At(graph, kEast));
}

TEST(OpeningsTest, AGapBesideAThickBlockIsNotAnOpening) {
  // A slope's box is sealed whole, so a gap by it is held by a block thicker than a wall.
  std::vector<CellBox> sealed = Shell();
  sealed.push_back({.min = {kWallX, 1, 1}, .extent = {1, 3, kHeight}});
  sealed.push_back({.min = {kWallX - 1, 6, 1}, .extent = {3, 2, kHeight}});
  sealed.push_back({.min = {kWallX, 8, 1}, .extent = {1, kSide - 7, kHeight}});

  const RoomGraph graph = Build({.sealed = sealed});

  EXPECT_TRUE(graph.openings.empty());
  EXPECT_EQ(At(graph, kWest), At(graph, kEast));
}

TEST(OpeningsTest, ASmallHoleInTheHullIsAPortalToTheVacuum) {
  std::vector<CellBox> sealed = Shell();
  sealed.erase(sealed.begin() + 3);
  sealed.push_back({.min = {kOuter - 1, 0, 1}, .extent = {1, 4, kHeight}});
  sealed.push_back({.min = {kOuter - 1, 6, 1}, .extent = {1, kOuter - 6, kHeight}});

  const RoomGraph graph = Build({.sealed = sealed});

  const RoomId inside = At(graph, kWest);
  ASSERT_NE(inside, kVacuumRoom);
  ASSERT_EQ(graph.portals.size(), 1U);
  EXPECT_EQ(std::min(graph.portals[0].a, graph.portals[0].b), kVacuumRoom);
  EXPECT_EQ(std::max(graph.portals[0].a, graph.portals[0].b), inside);
}

TEST(OpeningsTest, AHoleInTheFloorBetweenTwoLevelsIsAPortalAlongZ) {
  std::vector<CellBox> sealed = Shell();
  constexpr int kDeck = 3;
  sealed.push_back({.min = {1, 1, kDeck}, .extent = {kSide, 3, 1}});
  sealed.push_back({.min = {1, 6, kDeck}, .extent = {kSide, kSide - 5, 1}});
  sealed.push_back({.min = {1, 4, kDeck}, .extent = {3, 2, 1}});
  sealed.push_back({.min = {6, 4, kDeck}, .extent = {kSide - 5, 2, 1}});

  const RoomGraph graph = Build({.sealed = sealed});

  ASSERT_NE(At(graph, {5, 5, 2}), At(graph, {5, 5, 4}));
  ASSERT_EQ(graph.openings.size(), 1U);
  EXPECT_EQ(graph.openings[0].axis, Axis::kZ);
  EXPECT_EQ(graph.openings[0].opening, (CellBox {.min = {4, 4, kDeck}, .extent = {2, 2, 1}}));
}

TEST(OpeningsTest, TakingADoorOutAndPuttingItBackMatchesAFullBuild) {
  const std::vector<CellBox> solid = With(Shell(), PartitionWithHole(2, 4));
  const CellBox door {.min = {kWallX, 4, 1}, .extent = {1, 2, 4}};
  const RoomGraph closed = Build({.sealed = With(solid, {door})});
  ASSERT_TRUE(closed.openings.empty());

  const auto opened = UpdateRooms(closed, RoomSources {.sealed = solid}, closed.next_id, {});
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(opened->openings, Build({.sealed = solid}).openings);
  EXPECT_NE(At(*opened, kWest), At(*opened, kEast));

  const auto shut = UpdateRooms(*opened, RoomSources {.sealed = With(solid, {door})}, opened->next_id, {});
  ASSERT_TRUE(shut.has_value());
  EXPECT_TRUE(shut->openings.empty());
  EXPECT_TRUE(shut->portals.empty());
}

}  // namespace
}  // namespace z13::station::rooms
