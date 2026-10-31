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

#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/utils/frustum.h>
#include <rooms/room_graph.h>

namespace z13::station::rooms {

// A room the camera sees, and the frustum narrowed to the portals it is seen through.
struct SeenRoom {
  RoomId room {};
  // Nothing for the vacuum, which has no bounds.
  std::optional<Eigen::AlignedBox3f> bounds;
  z13::math::Frustum frustum;
};

struct CameraView {
  Eigen::Matrix4f view_projection = Eigen::Matrix4f::Identity();
  // In meters.
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  // A portal narrower than this on screen, in normalized device coordinates, hides its room.
  Eigen::Vector2f min_portal_extent = Eigen::Vector2f::Zero();
  // Openings of doors that are open, seen through (RoomView::opened); the span must outlive the ViewCulling's build.
  std::span<const z13::building::primitives::CellBox> opened;
};

// What the camera can see: its frustum and, in the station, the rooms seen through
// portals (rooms/room_visibility.h).
class ViewCulling {
 public:
  using OptionalGraph = z13::station::rooms::OptionalRoomGraph;

  // Without a graph, or with the eye inside a block, only the frustum culls; from the
  // vacuum, it alone culls blocks, and the rooms seen through windows pick the lamps.
  ViewCulling(const CameraView& view, OptionalGraph graph);

  bool Visible(const Eigen::AlignedBox3f& box) const;
  // Whether every box inside `box` is Visible: it lies wholly in the frustum and, in the station,
  // in one seen room's bounds and narrowed frustum.
  bool AllVisible(const Eigen::AlignedBox3f& box) const;

  // Sorted; nothing without a graph or with the eye inside a block.
  const std::optional<std::vector<RoomId>>& SeenRooms() const;
  std::optional<size_t> SeenRoomCount() const;

 private:
  z13::math::Frustum frustum_;
  std::optional<std::vector<RoomId>> seen_rooms_;
  // Also nothing in the vacuum, which has no bounds to cull by.
  std::optional<std::vector<SeenRoom>> culling_rooms_;
};

}  // namespace z13::station::rooms
