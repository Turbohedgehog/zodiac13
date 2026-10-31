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

#include <rooms/view_culling.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include <z13/components/block.h>
#include <rooms/room_visibility.h>

namespace z13::station::rooms {

namespace {

using z13::station::kCellSize;

// The blocks around a room lie just outside its free cells.
constexpr int kWallCells = 1;
constexpr int kSpaceAxes = 3;

std::optional<Eigen::AlignedBox3f> BoundsOf(const RoomGraph& graph, RoomId room) {
  const auto found = graph.FindRoom(room);
  if (room == kVacuumRoom || !found) {
    return std::nullopt;
  }
  const auto& cells = found->get().bounds;
  const Eigen::Vector3i low = cells.min - Eigen::Vector3i::Constant(kWallCells);
  const Eigen::Vector3i high = cells.End() + Eigen::Vector3i::Constant(kWallCells);
  return Eigen::AlignedBox3f(low.cast<float>() * kCellSize, high.cast<float>() * kCellSize);
}

Eigen::Vector3f EyeInCells(const CameraView& view) {
  return view.eye / kCellSize;
}

ScreenRegions RegionsInView(const CameraView& view, const RoomGraph& graph, RoomId eye_room) {
  const Eigen::Matrix4f cells_to_clip = view.view_projection * Eigen::Affine3f(Eigen::Scaling(kCellSize)).matrix();
  // What a window shows from the vacuum is drawn however small.
  const Eigen::Vector2f min_extent = eye_room == kVacuumRoom ? Eigen::Vector2f::Zero() : view.min_portal_extent;
  return VisibleRooms(graph, {.eye_room = eye_room,
                                                   .eye = EyeInCells(view),
                                                   .view_projection = cells_to_clip,
                                                   .screen = z13::math::Frustum::FullScreen(),
                                                   .min_extent = min_extent,
                                                   .opened = view.opened});
}

// Whether two boxes share a point. Raw floats, not Eigen: its Debug temporaries make this loop
// several times as slow (see Frustum::Intersects).
bool Overlap(const Eigen::AlignedBox3f& first, const Eigen::AlignedBox3f& second) {
  const float* first_low = first.min().data();
  const float* first_high = first.max().data();
  const float* second_low = second.min().data();
  const float* second_high = second.max().data();
  for (int axis = 0; axis < kSpaceAxes; ++axis) {
    if (first_low[axis] > second_high[axis] || second_low[axis] > first_high[axis]) {
      return false;
    }
  }
  return true;
}

std::vector<SeenRoom> CullingRooms(const CameraView& view, const RoomGraph& graph, const ScreenRegions& regions) {
  std::vector<SeenRoom> rooms;
  for (const auto& [room, region] : regions) {
    rooms.push_back({.room = room,
                     .bounds = BoundsOf(graph, room),
                     .frustum = z13::math::Frustum(view.view_projection, region)});
  }
  return rooms;
}

}  // namespace

ViewCulling::ViewCulling(const CameraView& view, OptionalGraph graph) : frustum_(view.view_projection) {
  if (!graph) {
    return;
  }
  const RoomGraph& rooms = graph->get();
  const auto eye_room = rooms.RoomAt(EyeInCells(view).array().floor().cast<int>());
  if (!eye_room) {
    return;
  }
  const ScreenRegions regions = RegionsInView(view, rooms, *eye_room);
  std::vector<RoomId> seen;
  std::ranges::transform(regions, std::back_inserter(seen), [](const auto& entry) { return entry.first; });
  std::ranges::sort(seen);
  seen_rooms_ = std::move(seen);
  if (*eye_room != kVacuumRoom) {
    culling_rooms_ = CullingRooms(view, rooms, regions);
  }
}

bool ViewCulling::Visible(const Eigen::AlignedBox3f& box) const {
  if (!frustum_.Intersects(box)) {
    return false;
  }
  return !culling_rooms_ || std::ranges::any_of(*culling_rooms_, [&box](const SeenRoom& room) {
    return (!room.bounds || Overlap(*room.bounds, box)) && room.frustum.Intersects(box);
  });
}

bool ViewCulling::AllVisible(const Eigen::AlignedBox3f& box) const {
  if (!frustum_.Contains(box)) {
    return false;
  }
  return !culling_rooms_ || std::ranges::any_of(*culling_rooms_, [&box](const SeenRoom& room) {
    return (!room.bounds || room.bounds->contains(box)) && room.frustum.Contains(box);
  });
}

const std::optional<std::vector<RoomId>>& ViewCulling::SeenRooms() const {
  return seen_rooms_;
}

std::optional<size_t> ViewCulling::SeenRoomCount() const {
  return seen_rooms_ ? std::optional(seen_rooms_->size()) : std::nullopt;
}

}  // namespace z13::station::rooms
