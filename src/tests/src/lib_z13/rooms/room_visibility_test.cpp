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

#include <cmath>
#include <numbers>
#include <vector>

#include <lib_core/utils/frustum.h>
#include <rooms/room_builder.h>
#include <rooms/room_visibility.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;

constexpr float kNear = 0.1f;
constexpr float kFar = 1000.f;

// Three rooms in a row along x, A [1, 8), B [9, 20) and C [21, 31), all 10 wide and
// 6 high, behind the walls at x = 8 and x = 20.
constexpr int kLength = 30;
constexpr int kSide = 10;
constexpr int kHeight = 6;
constexpr int kFirstWall = 8;
constexpr int kSecondWall = 20;

const Eigen::Vector3f kEyeInA(3.f, 5.5f, 4.5f);
// A one-cell window in line with the eye.
const Eigen::Vector3i kInLine(0, 5, 4);

std::vector<CellBox> Hall() {
  const Eigen::Vector3i outer(kLength + 2, kSide + 2, kHeight + 2);
  return {
      {.min = {0, 0, 0}, .extent = {outer.x(), outer.y(), 1}},
      {.min = {0, 0, outer.z() - 1}, .extent = {outer.x(), outer.y(), 1}},
      {.min = {0, 0, 1}, .extent = {1, outer.y(), kHeight}},
      {.min = {outer.x() - 1, 0, 1}, .extent = {1, outer.y(), kHeight}},
      {.min = {1, 0, 1}, .extent = {kLength, 1, kHeight}},
      {.min = {1, outer.y() - 1, 1}, .extent = {kLength, 1, kHeight}},
      {.min = {kFirstWall, 1, 1}, .extent = {1, kSide, kHeight}},
      {.min = {kSecondWall, 1, 1}, .extent = {1, kSide, kHeight}},
  };
}

PortalSource Window(int wall_x, const Eigen::Vector3i& at, const Eigen::Vector3i& extent, bool visible = true) {
  return {.opening = {.min = {wall_x, at.y(), at.z()}, .extent = {1, extent.y(), extent.z()}},
          .axis = Axis::kX,
          .visible = visible,
          .passable = false};
}

RoomGraph Build(const std::vector<PortalSource>& portals) {
  auto graph = BuildRooms({.sealed = Hall(), .portals = portals});
  EXPECT_TRUE(graph.has_value());
  return graph.value_or(RoomGraph {});
}

// OpenGL's perspective and look-at, 90° wide and high, with +Z up.
Eigen::Matrix4f ViewProjection(const Eigen::Vector3f& eye, const Eigen::Vector3f& forward) {
  const float f = 1.f / std::tan(std::numbers::pi_v<float> / 4.f);
  Eigen::Matrix4f projection = Eigen::Matrix4f::Zero();
  projection(0, 0) = f;
  projection(1, 1) = f;
  projection(2, 2) = (kFar + kNear) / (kNear - kFar);
  projection(2, 3) = 2.f * kFar * kNear / (kNear - kFar);
  projection(3, 2) = -1.f;

  const Eigen::Vector3f back = -forward.normalized();
  const Eigen::Vector3f side = Eigen::Vector3f::UnitZ().cross(back).normalized();
  const Eigen::Vector3f up = back.cross(side);
  Eigen::Matrix4f view = Eigen::Matrix4f::Identity();
  view.row(0).head<3>() = side;
  view.row(1).head<3>() = up;
  view.row(2).head<3>() = back;
  view.topRightCorner<3, 1>() = -(view.topLeftCorner<3, 3>() * eye);
  return projection * view;
}

ScreenRegions SeenFrom(const RoomGraph& graph, const Eigen::Vector3f& eye, const Eigen::Vector3f& forward) {
  const auto room = graph.RoomAt(eye.array().floor().cast<int>());
  EXPECT_TRUE(room.has_value());
  return VisibleRooms(graph, {.eye_room = room.value_or(kVacuumRoom),
                              .view_projection = ViewProjection(eye, forward),
                              .screen = z13::math::Frustum::FullScreen()});
}

bool Seen(const RoomGraph& graph, const ScreenRegions& seen, const Eigen::Vector3i& cell) {
  const auto room = graph.RoomAt(cell);
  return room && seen[*room].has_value();
}

const Eigen::Vector3i kCellInB(14, 5, 3);
const Eigen::Vector3i kCellInC(25, 5, 3);

TEST(RoomVisibilityTest, AWindowInViewShowsTheRoomBehindIt) {
  const RoomGraph graph = Build({Window(kFirstWall, kInLine, {1, 1, 1})});

  const ScreenRegions seen = SeenFrom(graph, kEyeInA, Eigen::Vector3f::UnitX());

  EXPECT_TRUE(Seen(graph, seen, kEyeInA.cast<int>()));
  EXPECT_TRUE(Seen(graph, seen, kCellInB));
  EXPECT_FALSE(Seen(graph, seen, kCellInC));
  EXPECT_FALSE(seen[kVacuumRoom].has_value());
}

TEST(RoomVisibilityTest, AWindowBehindTheEyeShowsNothing) {
  const RoomGraph graph = Build({Window(kFirstWall, kInLine, {1, 1, 1})});

  EXPECT_FALSE(Seen(graph, SeenFrom(graph, kEyeInA, -Eigen::Vector3f::UnitX()), kCellInB));
}

TEST(RoomVisibilityTest, AClosedDoorHidesTheRoomBehindIt) {
  const RoomGraph graph = Build({Window(kFirstWall, kInLine, {1, 1, 1}, false)});

  EXPECT_FALSE(Seen(graph, SeenFrom(graph, kEyeInA, Eigen::Vector3f::UnitX()), kCellInB));
}

TEST(RoomVisibilityTest, ASecondWindowShowsItsRoomOnlyInsideTheFirst) {
  const RoomGraph in_line = Build({Window(kFirstWall, kInLine, {1, 1, 1}), Window(kSecondWall, kInLine, {1, 1, 1})});
  const RoomGraph aside =
      Build({Window(kFirstWall, kInLine, {1, 1, 1}), Window(kSecondWall, {0, 1, 1}, {1, 2, 2})});

  EXPECT_TRUE(Seen(in_line, SeenFrom(in_line, kEyeInA, Eigen::Vector3f::UnitX()), kCellInC));
  EXPECT_FALSE(Seen(aside, SeenFrom(aside, kEyeInA, Eigen::Vector3f::UnitX()), kCellInC));
}

TEST(RoomVisibilityTest, AWindowBesideTheEyeAndPartlyBehindItShowsNothing) {
  const RoomGraph graph = Build({Window(kFirstWall, kInLine, {1, 1, 1})});

  // Just past the window in B, looking along the wall that holds it.
  const ScreenRegions seen = SeenFrom(graph, {10.f, 5.5f, 4.5f}, Eigen::Vector3f::UnitY());

  EXPECT_FALSE(Seen(graph, seen, kEyeInA.cast<int>()));
}

TEST(RoomVisibilityTest, AnEyeInTheOpeningSeesThroughAllOfIt) {
  const RoomGraph graph = Build({Window(kFirstWall, kInLine, {1, 1, 1})});
  const ScreenRegions seen =
      VisibleRooms(graph, {.eye_room = *graph.RoomAt(kEyeInA.cast<int>()),
                           .view_projection = ViewProjection({8.5f, 5.5f, 4.5f}, Eigen::Vector3f::UnitX()),
                           .screen = z13::math::Frustum::FullScreen()});

  ASSERT_TRUE(Seen(graph, seen, kCellInB));
  EXPECT_TRUE(seen[*graph.RoomAt(kCellInB)]->isApprox(z13::math::Frustum::FullScreen()));
}

}  // namespace
}  // namespace z13::station::rooms
