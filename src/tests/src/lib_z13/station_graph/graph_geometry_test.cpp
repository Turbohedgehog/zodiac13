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

#include <string>

#include <station_graph/graph_geometry.h>
#include <station_graph/graph_pick.h>
#include <z13_tests/shipped_station.h>

namespace z13::editor::graph {
namespace {

constexpr int kRoomCells = 12;
constexpr int kTallHeight = 20;
constexpr int kSlabsCells = 2;  // the floor and the ceiling around a room's free height

PropertySchema Schema() {
  const auto schema = z13::testing::ShippedPropertySchema();
  EXPECT_TRUE(schema) << schema.error();
  return schema.value_or(PropertySchema {});
}

Node RoomAt(std::string id, int x, int y) {
  return {.id = std::move(id), .box = {.origin = {x, y, 0}, .size = {kRoomCells, kRoomCells}}};
}

TEST(GraphGeometryTest, ARoomTakesItsBoxAndItsHeightPlusTheSlabs) {
  const PropertySchema schema = Schema();
  Node room = RoomAt("a", 3, 4);
  room.properties[std::string(kHeightProperty)] = kTallHeight;

  const CellBox cells = CellsOfRoom(room, schema);

  EXPECT_EQ(cells.min, Eigen::Vector3i(3, 4, 0));
  EXPECT_EQ(cells.extent, Eigen::Vector3i(kRoomCells, kRoomCells, kTallHeight + kSlabsCells));
  EXPECT_EQ(HeightOf(RoomAt("b", 0, 0), schema), std::get<int>(schema.node.at(std::string(kHeightProperty)).default_value));
}

TEST(GraphGeometryTest, RoomsMeetingByAWallShareItsInnerStretchAsTallAsTheTallerRoom) {
  const PropertySchema schema = Schema();
  Node tall = RoomAt("b", kRoomCells - 1, 0);
  tall.properties[std::string(kHeightProperty)] = kTallHeight;

  const auto wall = SharedWall(RoomAt("a", 0, 0), tall, schema);

  ASSERT_TRUE(wall);
  EXPECT_EQ(wall->min, Eigen::Vector3i(kRoomCells - 1, 1, 0));
  EXPECT_EQ(wall->extent, Eigen::Vector3i(1, kRoomCells - 2, kTallHeight + kSlabsCells));
  EXPECT_EQ(SharedWall(tall, RoomAt("a", 0, 0), schema), wall) << "the order of the rooms does not matter";
}

TEST(GraphGeometryTest, RoomsApartTouchingByACornerOrOnAnotherLevelShareNoWall) {
  const PropertySchema schema = Schema();
  Node upper = RoomAt("upper", kRoomCells - 1, 0);
  upper.box.origin.z() = kRoomCells;

  EXPECT_FALSE(SharedWall(RoomAt("a", 0, 0), RoomAt("far", kRoomCells * 3, 0), schema));
  EXPECT_FALSE(SharedWall(RoomAt("a", 0, 0), RoomAt("corner", kRoomCells - 1, kRoomCells - 1), schema));
  EXPECT_FALSE(SharedWall(RoomAt("a", 0, 0), upper, schema));
}

Graph TwoRoomsWithADoor() {
  return {.nodes = {RoomAt("a", 0, 0), RoomAt("b", kRoomCells - 1, 0)}, .edges = {{.id = "door", .from = "a", .to = "b"}}};
}

TEST(GraphPickTest, ARayFromOutsideMeetsTheRoomItEnters) {
  const auto pick = PickAlong(TwoRoomsWithADoor(), Schema(), Eigen::Vector3f(-5.f, 6.f, 2.f), Eigen::Vector3f::UnitX());

  ASSERT_TRUE(pick);
  EXPECT_EQ(pick->kind, PickKind::kRoom);
  EXPECT_EQ(pick->id, "a");
  EXPECT_FLOAT_EQ(pick->distance, 5.f);
}

TEST(GraphPickTest, ARayThatCrossesAnOpeningsWallBeforeLeavingTheRoomPicksTheOpening) {
  const auto pick = PickAlong(TwoRoomsWithADoor(), Schema(), Eigen::Vector3f(5.f, 6.f, 2.f), Eigen::Vector3f::UnitX());

  ASSERT_TRUE(pick);
  EXPECT_EQ(pick->kind, PickKind::kEdge);
  EXPECT_EQ(pick->id, "door");
}

TEST(GraphPickTest, ARayAwayFromTheOpeningPicksTheRoomItStartsIn) {
  const auto pick = PickAlong(TwoRoomsWithADoor(), Schema(), Eigen::Vector3f(5.f, 6.f, 2.f), Eigen::Vector3f::UnitY());

  ASSERT_TRUE(pick);
  EXPECT_EQ(pick->kind, PickKind::kRoom);
  EXPECT_EQ(pick->id, "a");
  EXPECT_FLOAT_EQ(pick->distance, 0.f);
}

TEST(GraphPickTest, ARayThatMeetsNothingPicksNothing) {
  EXPECT_FALSE(PickAlong(TwoRoomsWithADoor(), Schema(), Eigen::Vector3f(-5.f, 100.f, 2.f), Eigen::Vector3f::UnitX()));
  EXPECT_FALSE(PickAlong(TwoRoomsWithADoor(), Schema(), Eigen::Vector3f(-5.f, 6.f, 2.f), -Eigen::Vector3f::UnitX()));
}

}  // namespace
}  // namespace z13::editor::graph
