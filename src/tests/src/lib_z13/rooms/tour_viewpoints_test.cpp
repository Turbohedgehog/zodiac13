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

#include <vector>

#include <rooms/room_builder.h>
#include <rooms/tour_viewpoints.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;

constexpr int kLength = 20;
constexpr int kSide = 10;
constexpr int kHeight = 10;
// Splits the hall into a small room [1, 5) and a large one [6, 21) along x.
constexpr int kWall = 5;
constexpr size_t kHeadingsPerRoom = 4;

RoomGraph Hall() {
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
  auto graph = BuildRooms({.sealed = sealed});
  EXPECT_TRUE(graph.has_value());
  return graph.value_or(RoomGraph {});
}

TEST(TourViewpointsTest, LooksAroundEachRoomFromInsideItThenAtTheStationFromOutside) {
  const RoomGraph graph = Hall();
  ASSERT_EQ(graph.rooms.size(), 3U);

  const std::vector<Viewpoint> viewpoints = TourViewpoints(graph, 2);

  ASSERT_EQ(viewpoints.size(), (2 * kHeadingsPerRoom) + 1);
  for (size_t i = 0; i + 1 < viewpoints.size(); ++i) {
    EXPECT_EQ(graph.RoomAt(viewpoints[i].eye.array().floor().cast<int>()), viewpoints[i].room) << i;
  }
  EXPECT_EQ(viewpoints.back().room, kVacuumRoom);
  EXPECT_EQ(graph.RoomAt(viewpoints.back().eye.array().floor().cast<int>()), kVacuumRoom);
}

TEST(TourViewpointsTest, GoesFromTheSmallestRoomToTheLargest) {
  const RoomGraph graph = Hall();

  const std::vector<Viewpoint> viewpoints = TourViewpoints(graph, 2);

  ASSERT_GT(viewpoints.size(), kHeadingsPerRoom);
  EXPECT_LT(graph.rooms[viewpoints.front().room].volume_cells,
            graph.rooms[viewpoints[kHeadingsPerRoom].room].volume_cells);
}

TEST(TourViewpointsTest, FirstLooksTowardsTheRoomsCentre) {
  const RoomGraph graph = Hall();

  const Viewpoint first = TourViewpoints(graph, 1).front();

  const CellBox& bounds = graph.rooms[first.room].bounds;
  const Eigen::Vector3f centre = (bounds.min.cast<float>() + bounds.End().cast<float>()) / 2.f;
  EXPECT_GT((first.target - first.eye).head<2>().dot((centre - first.eye).head<2>()), 0.f);
}

TEST(TourViewpointsTest, AStationWithoutRoomsIsOnlySeenFromOutside) {
  const RoomGraph graph = BuildRooms({.sealed = {{.min = {0, 0, 0}, .extent = {4, 4, 1}}}}).value();

  const std::vector<Viewpoint> viewpoints = TourViewpoints(graph, 3);

  ASSERT_EQ(viewpoints.size(), 1U);
  EXPECT_EQ(viewpoints.front().room, kVacuumRoom);
}

}  // namespace
}  // namespace z13::station::rooms
