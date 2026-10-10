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

#include <rooms/room_visibility.h>

#include <array>
#include <cstddef>

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;

// Eigen numbers a box's corners with one bit per axis, set for the high side.
constexpr size_t kBoxCorners = 8;
constexpr std::array<size_t, 3> kAxisBits {1, 2, 4};
// Points are kept this far in front of the eye's plane, where projecting still works.
constexpr float kMinDepth = 1e-4f;

// The part of `seen` the opening covers: its corners in front of the eye, and where its
// edges cross into the eye's plane, so an opening beside the eye is not taken whole.
std::optional<Eigen::AlignedBox2f> Through(
    const Eigen::AlignedBox2f& seen, const CellBox& opening, const Eigen::Matrix4f& view_projection) {
  const Eigen::AlignedBox3f box(opening.min.cast<float>(), opening.End().cast<float>());
  std::array<Eigen::Vector4f, kBoxCorners> clip;
  for (size_t i = 0; i < kBoxCorners; ++i) {
    clip[i] = view_projection * box.corner(static_cast<Eigen::AlignedBox3f::CornerType>(i)).homogeneous();
  }
  Eigen::AlignedBox2f covered;
  const auto add = [&covered](const Eigen::Vector4f& point) { covered.extend(point.head<2>() / point.w()); };
  for (size_t i = 0; i < kBoxCorners; ++i) {
    const bool front = clip[i].w() > kMinDepth;
    if (front) {
      add(clip[i]);
    }
    for (const size_t bit : kAxisBits) {
      const size_t j = i | bit;
      if (j != i && front != (clip[j].w() > kMinDepth)) {
        const float t = (kMinDepth - clip[i].w()) / (clip[j].w() - clip[i].w());
        add(clip[i] + (t * (clip[j] - clip[i])));
      }
    }
  }
  if (covered.isEmpty()) {
    return std::nullopt;
  }
  const Eigen::AlignedBox2f narrowed = seen.intersection(covered);
  return narrowed.isEmpty() ? std::nullopt : std::optional(narrowed);
}

}  // namespace

// A room reached again through another portal grows its region and is walked again;
// regions only grow, from a finite set of edges, so the walk ends.
ScreenRegions VisibleRooms(const RoomGraph& graph, const RoomView& view) {
  ScreenRegions seen(graph.rooms.size());
  if (view.eye_room >= graph.room_portals.size()) {
    return seen;
  }
  seen[view.eye_room] = view.screen;
  std::vector<RoomIndex> pending {view.eye_room};
  while (!pending.empty()) {
    const RoomIndex room = pending.back();
    pending.pop_back();
    for (const size_t index : graph.room_portals[room]) {
      const Portal& portal = graph.portals[index];
      if (!portal.visible || portal.a == portal.b) {
        continue;
      }
      const RoomIndex other = portal.a == room ? portal.b : portal.a;
      const auto through = Through(*seen[room], portal.opening, view.view_projection);
      std::optional<Eigen::AlignedBox2f>& region = seen[other];
      if (!through || (region && region->contains(*through))) {
        continue;
      }
      region = region ? region->merged(*through) : *through;
      pending.push_back(other);
    }
  }
  return seen;
}

}  // namespace z13::station::rooms
