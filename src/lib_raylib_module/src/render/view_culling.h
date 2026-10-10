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
#include <vector>

#include <Eigen/Dense>

#include <lib_core/utils/frustum.h>
#include <rooms/room_graph.h>

namespace z13::raylib {

// A room the camera sees, and the frustum narrowed to the portals it is seen through.
struct SeenRoom {
  z13::station::rooms::RoomIndex room {};
  // Nothing for the vacuum, which has no bounds.
  std::optional<Eigen::AlignedBox3f> bounds;
  z13::math::Frustum frustum;
};

// What the camera can see: its frustum and, in the station, the rooms seen through
// portals (rooms/room_visibility.h).
class ViewCulling {
 public:
  using OptionalGraph = std::optional<std::reference_wrapper<const z13::station::rooms::RoomGraph>>;

  // Without a graph, or with the eye inside a block or in the vacuum, only the frustum culls.
  ViewCulling(const Eigen::Matrix4f& view_projection, const Eigen::Vector3f& eye, OptionalGraph graph);

  bool Visible(const Eigen::AlignedBox3f& box) const;

  // Sorted; nothing while rooms don't cull.
  std::optional<std::vector<z13::station::rooms::RoomIndex>> SeenRooms() const;
  std::optional<size_t> SeenRoomCount() const;

 private:
  z13::math::Frustum frustum_;
  std::optional<std::vector<SeenRoom>> rooms_;
};

}  // namespace z13::raylib
