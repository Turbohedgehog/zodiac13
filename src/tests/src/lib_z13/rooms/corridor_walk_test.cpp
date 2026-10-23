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

#include <Eigen/Dense>

#include <rooms/corridor_walk.h>
#include <rooms/room_builder.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;

constexpr int kSide = 10;
constexpr int kHeight = 12;
constexpr int kRoomWidth = 8;

// A hall of three rooms side by side along x (their walls at x = 9 and x = 18), a doorway in each.
RoomSources Hall() {
  constexpr int length = (3 * kRoomWidth) + 4;
  RoomSources sources {.sealed = {
      {.min = {0, 0, 0}, .extent = {length, kSide + 2, 1}},
      {.min = {0, 0, kHeight + 1}, .extent = {length, kSide + 2, 1}},
      {.min = {0, 0, 1}, .extent = {1, kSide + 2, kHeight}},
      {.min = {length - 1, 0, 1}, .extent = {1, kSide + 2, kHeight}},
      {.min = {1, 0, 1}, .extent = {length - 2, 1, kHeight}},
      {.min = {1, kSide + 1, 1}, .extent = {length - 2, 1, kHeight}},
  }};
  for (const int wall : {kRoomWidth + 1, (2 * kRoomWidth) + 2}) {
    sources.sealed.push_back({.min = {wall, 1, 1}, .extent = {1, kSide, kHeight}});
    sources.portals.push_back({.opening = {.min = {wall, 4, 1}, .extent = {1, 3, 8}}, .axis = Axis::kX,
                               .visible = true, .passable = true});
  }
  return sources;
}

RoomGraph HallGraph() {
  return BuildRooms(Hall()).value();
}

const Eigen::Vector3f kStart {3.5f, 5.5f, 7.5f};

TEST(CorridorWalkTest, WalksFromTheStartRoomToTheFarthestOneThroughEachDoorway) {
  const RoomGraph graph = HallGraph();

  const std::vector<Viewpoint> walk = CorridorWalk(graph, kStart, 8);

  ASSERT_EQ(walk.size(), 3U);
  EXPECT_EQ(walk.front().eye, kStart);
  EXPECT_NE(walk[0].room, walk[1].room);
  EXPECT_NE(walk[1].room, walk[2].room);
  for (const Viewpoint& viewpoint : walk) {
    EXPECT_EQ(graph.RoomAt(viewpoint.eye.array().floor().cast<int>()), viewpoint.room);
  }
  // Each looks on towards the next doorway, the last at the middle of its room.
  EXPECT_GT(walk[0].target.x(), walk[0].eye.x());
  EXPECT_GT(walk[1].target.x(), walk[1].eye.x());
  EXPECT_GT(walk[2].eye.x(), walk[1].eye.x());
}

TEST(CorridorWalkTest, StopsAfterTheRoomsItMayEnter) {
  const std::vector<Viewpoint> walk = CorridorWalk(HallGraph(), kStart, 1);

  EXPECT_EQ(walk.size(), 2U);
}

TEST(CorridorWalkTest, AStartInNoRoomHasNoWalk) {
  EXPECT_TRUE(CorridorWalk(HallGraph(), {0.5f, 5.5f, 7.5f}, 8).empty());
}

TEST(CorridorWalkTest, TheSameGraphGivesTheSameWalk) {
  const std::vector<Viewpoint> first = CorridorWalk(HallGraph(), kStart, 8);
  const std::vector<Viewpoint> again = CorridorWalk(HallGraph(), kStart, 8);

  ASSERT_EQ(first.size(), again.size());
  for (size_t i = 0; i < first.size(); ++i) {
    EXPECT_EQ(first[i].eye, again[i].eye);
    EXPECT_EQ(first[i].target, again[i].target);
  }
}

}  // namespace
}  // namespace z13::station::rooms
