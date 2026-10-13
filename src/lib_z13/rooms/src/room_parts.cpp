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


#include "room_parts.h"

#include <algorithm>
#include <tuple>

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;
using z13::building::primitives::kCellAxes;


// The leaves in `side`, one cell thick across `axis`, as patches on the face.
std::vector<FacePatch> PatchesOn(const RoomGraph& graph, const CellBox& side, size_t axis) {
  const auto u = static_cast<Eigen::Index>((axis + 1) % kCellAxes);
  const auto v = static_cast<Eigen::Index>((axis + 2) % kCellAxes);
  std::vector<FacePatch> patches;
  for (const LeafRef& ref : graph.space.LeavesIn(side)) {
    const CellBox cut = graph.space.Leaf(ref).Intersection(side);
    patches.push_back(
        {.min = {cut.min[u], cut.min[v]}, .end = {cut.End()[u], cut.End()[v]}, .room = graph.RoomOf(ref)});
  }
  return patches;
}

int OverlapArea(const FacePatch& a, const FacePatch& b) {
  const Eigen::Vector2i size = a.end.cwiseMin(b.end) - a.min.cwiseMax(b.min);
  return (size.array() > 0).all() ? size.x() * size.y() : 0;
}

Eigen::Vector3<int64_t> CellSum(const CellBox& box) {
  const Eigen::Array3<int64_t> min = box.min.cast<int64_t>();
  const Eigen::Array3<int64_t> extent = box.extent.cast<int64_t>();
  // min + ... + (min + extent - 1) along the axis, once per cell across it.
  const Eigen::Array3<int64_t> series = extent * (2 * min + extent - 1) / 2;
  return (series * (extent.prod() / extent)).matrix();
}

}  // namespace

RoomExtent ExtentOf(const CellBox& leaf) {
  return {.anchor = leaf.min,
          .low = leaf.min,
          .high = leaf.End(),
          .volume = leaf.extent.cast<int64_t>().prod(),
          .cell_sum = CellSum(leaf)};
}

void Absorb(RoomExtent& room, const CellBox& leaf) {
  if (AnchorBefore(leaf.min, room.anchor)) {
    room.anchor = leaf.min;
  }
  room.low = room.low.cwiseMin(leaf.min);
  room.high = room.high.cwiseMax(leaf.End());
  room.volume += leaf.extent.cast<int64_t>().prod();
  room.cell_sum += CellSum(leaf);
}

Room ToRoom(const RoomExtent& extent) {
  return {.anchor = extent.anchor,
          .volume_cells = extent.volume,
          .bounds = {.min = extent.low, .extent = extent.high - extent.low},
          .cell_sum = extent.cell_sum};
}

// The rooms on the two sides of the opening's face, counted per pair of rooms by the cells
// where the leaves on both sides meet.
std::vector<Portal> PortalsThrough(const RoomGraph& graph, const PortalSource& source) {
  const auto axis = static_cast<size_t>(source.axis);
  const auto index = static_cast<Eigen::Index>(axis);
  CellBox before = source.opening;
  before.extent[index] = 1;
  --before.min[index];
  CellBox after = before;
  after.min[index] += source.opening.extent[index] + 1;
  const std::vector<FacePatch> low_patches = PatchesOn(graph, before, axis);
  const std::vector<FacePatch> high_patches = PatchesOn(graph, after, axis);
  std::vector<OpeningShare> shares;
  for (const FacePatch& low : low_patches) {
    for (const FacePatch& high : high_patches) {
      const int area = OverlapArea(low, high);
      if (area == 0 || low.room == high.room) {
        continue;
      }
      const std::pair pair {std::min(low.room, high.room), std::max(low.room, high.room)};
      const auto found = std::ranges::find(shares, pair, &OpeningShare::rooms);
      if (found == shares.end()) {
        shares.push_back({.rooms = pair, .area_cells = area, .low_side = low.room});
      } else {
        found->area_cells += area;
      }
    }
  }
  std::ranges::sort(shares, {}, &OpeningShare::rooms);
  std::vector<Portal> portals;
  for (const OpeningShare& share : shares) {
    portals.push_back({.a = share.rooms.first,
                       .b = share.rooms.second,
                       .area_cells = share.area_cells,
                       .visible = source.visible,
                       .passable = source.passable,
                       .opening = source.opening,
                       .axis = source.axis,
                       .low_side = share.low_side});
  }
  return portals;
}

bool PortalBefore(const Portal& first, const Portal& second) {
  if (first.opening.min != second.opening.min) {
    return AnchorBefore(first.opening.min, second.opening.min);
  }
  return std::tuple(first.a, first.b) < std::tuple(second.a, second.b);
}

std::unordered_map<RoomId, std::vector<size_t>> PortalsByRoom(const std::vector<Portal>& portals) {
  std::unordered_map<RoomId, std::vector<size_t>> by_room;
  for (size_t i = 0; i < portals.size(); ++i) {
    by_room[portals[i].a].push_back(i);
    by_room[portals[i].b].push_back(i);
  }
  return by_room;
}

}  // namespace z13::station::rooms
