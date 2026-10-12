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

#include "room_box_cache.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace z13::station {

namespace {

using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomId;

constexpr Eigen::Index kAxisCount = Eigen::Vector3i::SizeAtCompileTime;

bool MergeAlong(std::vector<OverlayBox>& boxes, Eigen::Index axis) {
  const Eigen::Index u = (axis + 1) % kAxisCount;
  const Eigen::Index v = (axis + 2) % kAxisCount;
  const auto key = [&](const OverlayBox& box) {
    return std::tuple(box.min[u], box.extent[u], box.min[v], box.extent[v], box.min[axis]);
  };
  std::ranges::sort(boxes, {}, key);
  std::vector<OverlayBox> merged;
  for (const OverlayBox& box : boxes) {
    if (!merged.empty()) {
      OverlayBox& last = merged.back();
      if (last.min[u] == box.min[u] && last.extent[u] == box.extent[u] && last.min[v] == box.min[v] &&
          last.extent[v] == box.extent[v] && last.min[axis] + last.extent[axis] == box.min[axis]) {
        last.extent[axis] += box.extent[axis];
        continue;
      }
    }
    merged.push_back(box);
  }
  const bool joined = merged.size() < boxes.size();
  boxes = std::move(merged);
  return joined;
}

void MergeBoxes(std::vector<OverlayBox>& boxes) {
  bool joined = true;
  while (joined) {
    joined = false;
    for (Eigen::Index axis = 0; axis < kAxisCount; ++axis) {
      joined = MergeAlong(boxes, axis) || joined;
    }
  }
}

std::vector<OverlayBox> RoomBoxes(const RoomGraph& graph, RoomId room) {
  std::vector<OverlayBox> boxes;
  for (const rooms::LeafRef& leaf : graph.space.LeavesIn(graph.rooms.at(room).bounds)) {
    if (graph.RoomOf(leaf) == room) {
      const auto& cells = graph.space.Leaf(leaf);
      boxes.push_back({.min = cells.min, .extent = cells.extent, .color = kRoomOverlayColor});
    }
  }
  MergeBoxes(boxes);
  return boxes;
}

}  // namespace

const std::vector<OverlayBox>& RoomBoxCache::BoxesOf(const RoomGraph& graph, uint64_t fingerprint, RoomId room) {
  if (fingerprint_ != fingerprint) {
    boxes_ = {};
    fingerprint_ = fingerprint;
  }
  auto found = boxes_.find(room);
  if (found == boxes_.end()) {
    found = boxes_.emplace(room, RoomBoxes(graph, room)).first;
  }
  return found->second;
}

}  // namespace z13::station
