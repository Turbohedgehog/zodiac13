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

#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <rooms/room_graph.h>

namespace z13::station::rooms {

// Per room (by RoomIndex), the part of the screen in normalized device coordinates it is
// seen through; nothing for a room out of sight.
using ScreenRegions = std::vector<std::optional<Eigen::AlignedBox2f>>;

struct RoomView {
  RoomIndex eye_room {};
  // In cells.
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  // Maps cells into OpenGL clip space.
  Eigen::Matrix4f view_projection = Eigen::Matrix4f::Identity();
  // What the eye room is seen through, usually the whole screen.
  Eigen::AlignedBox2f screen;
  // A portal seen narrower than this on either axis shows nothing worth drawing.
  Eigen::Vector2f min_extent = Eigen::Vector2f::Zero();
};

// Walks the visible portals from the eye room, narrowing the screen to each opening. A
// portal is passed only from the side the eye is on: a window seen from behind leads nowhere.
ScreenRegions VisibleRooms(const RoomGraph& graph, const RoomView& view);

}  // namespace z13::station::rooms
