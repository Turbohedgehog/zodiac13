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

#include "view_culling.h"

#include <algorithm>
#include <iterator>

#include <z13/components/station.h>
#include <rooms/room_visibility.h>

namespace z13::raylib {

namespace {

using z13::station::kCellSize;
using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomIndex;

// The blocks around a room lie just outside its free cells.
constexpr int kWallCells = 1;

std::optional<Eigen::AlignedBox3f> BoundsOf(const RoomGraph& graph, RoomIndex room) {
  if (room == z13::station::rooms::kVacuumRoom) {
    return std::nullopt;
  }
  const auto& cells = graph.rooms[room].bounds;
  const Eigen::Vector3i low = cells.min - Eigen::Vector3i::Constant(kWallCells);
  const Eigen::Vector3i high = cells.End() + Eigen::Vector3i::Constant(kWallCells);
  return Eigen::AlignedBox3f(low.cast<float>() * kCellSize, high.cast<float>() * kCellSize);
}

std::optional<std::vector<SeenRoom>> RoomsInView(
    const Eigen::Matrix4f& view_projection, const Eigen::Vector3f& eye, const RoomGraph& graph) {
  const auto eye_room = graph.RoomAt((eye / kCellSize).array().floor().cast<int>());
  if (!eye_room) {
    return std::nullopt;
  }
  const Eigen::Matrix4f cells_to_clip = view_projection * Eigen::Affine3f(Eigen::Scaling(kCellSize)).matrix();
  const z13::station::rooms::ScreenRegions regions = z13::station::rooms::VisibleRooms(
      graph, {.eye_room = *eye_room, .view_projection = cells_to_clip, .screen = z13::math::Frustum::FullScreen()});
  std::vector<SeenRoom> seen;
  for (RoomIndex room = 0; room < regions.size(); ++room) {
    if (regions[room]) {
      seen.push_back({.room = room,
                      .bounds = BoundsOf(graph, room),
                      .frustum = z13::math::Frustum(view_projection, *regions[room])});
    }
  }
  return seen;
}

}  // namespace

ViewCulling::ViewCulling(const Eigen::Matrix4f& view_projection, const Eigen::Vector3f& eye, OptionalGraph graph)
    : frustum_(view_projection), rooms_(graph ? RoomsInView(view_projection, eye, *graph) : std::nullopt) {
}

bool ViewCulling::Visible(const Eigen::AlignedBox3f& box) const {
  if (!frustum_.Intersects(box)) {
    return false;
  }
  return !rooms_ || std::ranges::any_of(*rooms_, [&box](const SeenRoom& room) {
    return (!room.bounds || room.bounds->intersects(box)) && room.frustum.Intersects(box);
  });
}

std::optional<std::vector<RoomIndex>> ViewCulling::SeenRooms() const {
  if (!rooms_) {
    return std::nullopt;
  }
  std::vector<RoomIndex> seen;
  std::ranges::transform(*rooms_, std::back_inserter(seen), &SeenRoom::room);
  return seen;
}

std::optional<size_t> ViewCulling::SeenRoomCount() const {
  return rooms_ ? std::optional(rooms_->size()) : std::nullopt;
}

}  // namespace z13::raylib
