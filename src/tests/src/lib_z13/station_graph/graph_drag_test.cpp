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

#include <station_graph/graph_drag.h>

namespace z13::editor::graph {
namespace {

const RoomBox kBox {.origin = Eigen::Vector3i(10, 20, 0), .size = Eigen::Vector2i(8, 6)};
const Eigen::Vector2f kMiddle(14.f, 23.f);
const Eigen::Vector2f kNearWest(10.4f, 23.f);
const Eigen::Vector2f kNearNorth(14.f, 25.5f);
const Eigen::Vector2i kRight(3, 0);
const Eigen::Vector2i kUp(0, 4);
constexpr int kFarOver = 100;

TEST(GraphDragTest, TheMiddleOfARoomMovesIt) {
  const Grip grip = GripAt(kBox, kMiddle);

  const RoomBox moved = Dragged(kBox, grip, Eigen::Vector2i(2, -3));

  EXPECT_EQ(moved.origin, Eigen::Vector3i(12, 17, 0));
  EXPECT_EQ(moved.size, kBox.size);
}

TEST(GraphDragTest, ANearSidePullsOnlyThatSide) {
  const RoomBox west = Dragged(kBox, GripAt(kBox, kNearWest), kRight);
  const RoomBox north = Dragged(kBox, GripAt(kBox, kNearNorth), kUp);

  EXPECT_EQ(west.origin, Eigen::Vector3i(13, 20, 0));
  EXPECT_EQ(west.size, Eigen::Vector2i(5, 6));
  EXPECT_EQ(north.origin, kBox.origin);
  EXPECT_EQ(north.size, Eigen::Vector2i(8, 10));
}

TEST(GraphDragTest, ASideNeverCrossesTheOppositeOne) {
  const RoomBox west = Dragged(kBox, GripAt(kBox, kNearWest), Eigen::Vector2i(kFarOver, 0));
  const RoomBox north = Dragged(kBox, GripAt(kBox, kNearNorth), Eigen::Vector2i(0, -kFarOver));

  EXPECT_EQ(west.origin.x() + west.size.x(), kBox.origin.x() + kBox.size.x());
  EXPECT_EQ(west.size.x(), 1);
  EXPECT_EQ(north.origin, kBox.origin);
  EXPECT_EQ(north.size.y(), 1);
}

}  // namespace
}  // namespace z13::editor::graph
