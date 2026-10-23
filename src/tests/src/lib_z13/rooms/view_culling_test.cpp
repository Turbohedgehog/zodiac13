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

#include <Eigen/Dense>

#include <rooms/room_builder.h>
#include <rooms/view_culling.h>
#include <z13/components/block.h>
#include <z13_tests/camera_view.h>

namespace z13::station::rooms {
namespace {

constexpr int kSide = 10;
constexpr int kHeight = 8;

// A closed room, inside [1, kSide + 1) in x and y, with a window in its x = 0 wall to space.
RoomSources RoomWithWindow() {
  constexpr int outer = kSide + 2;
  RoomSources sources {.sealed = {
      {.min = {0, 0, 0}, .extent = {outer, outer, 1}},
      {.min = {0, 0, kHeight + 1}, .extent = {outer, outer, 1}},
      {.min = {0, 0, 1}, .extent = {1, outer, kHeight}},
      {.min = {outer - 1, 0, 1}, .extent = {1, outer, kHeight}},
      {.min = {1, 0, 1}, .extent = {kSide, 1, kHeight}},
      {.min = {1, outer - 1, 1}, .extent = {kSide, 1, kHeight}},
  }};
  sources.portals.push_back({.opening = {.min = {0, 3, 2}, .extent = {1, 4, 4}}, .axis = Axis::kX, .visible = true});
  return sources;
}

Eigen::AlignedBox3f BoxAround(const Eigen::Vector3f& centre_cells, float half_cells = 0.5f) {
  return {(centre_cells.array() - half_cells).matrix() * kCellSize,
          (centre_cells.array() + half_cells).matrix() * kCellSize};
}

TEST(ViewCullingTest, WithoutRoomsOnlyTheFrustumCulls) {
  const ViewCulling culling(z13::testing::CameraViewFrom({0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}), std::nullopt);

  EXPECT_TRUE(culling.Visible(BoxAround({20.f, 0.f, 0.f})));
  EXPECT_FALSE(culling.Visible(BoxAround({-20.f, 0.f, 0.f})));
  EXPECT_FALSE(culling.SeenRoomCount().has_value());
}

TEST(ViewCullingTest, InARoomOnlyWhatIsInItsSeenRoomsIsVisible) {
  const RoomGraph graph = BuildRooms(RoomWithWindow()).value();
  const Eigen::Vector3f eye_cells {3.5f, 5.5f, 4.5f};
  const Eigen::Vector3f target_cells = eye_cells + Eigen::Vector3f(5.f, 0.f, 0.f);
  const ViewCulling culling(
      z13::testing::CameraViewFrom(eye_cells * kCellSize, target_cells * kCellSize), std::cref(graph));

  EXPECT_EQ(culling.SeenRoomCount(), 1U);
  EXPECT_TRUE(culling.Visible(BoxAround({9.5f, 5.5f, 4.5f})));
  EXPECT_FALSE(culling.Visible(BoxAround({-9.5f, 5.5f, 4.5f})));
}

TEST(ViewCullingTest, FromSpaceTheRoomBehindAWindowIsSeenAndTheWallsAroundItAreNot) {
  const RoomGraph graph = BuildRooms(RoomWithWindow()).value();
  const Eigen::Vector3f eye_cells {-12.5f, 5.f, 4.f};
  const Eigen::Vector3f target_cells {5.f, 5.f, 4.f};
  const ViewCulling culling(
      z13::testing::CameraViewFrom(eye_cells * kCellSize, target_cells * kCellSize), std::cref(graph));

  ASSERT_TRUE(culling.SeenRoomCount().has_value());
  EXPECT_EQ(culling.SeenRoomCount(), 2U) << "space and the room";
  EXPECT_TRUE(culling.Visible(BoxAround({6.5f, 5.f, 4.f})));
}

}  // namespace
}  // namespace z13::station::rooms
