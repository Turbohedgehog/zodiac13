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


#include <rooms/room_overlap.h>

#include <map>
#include <utility>

#include "cell_boxes.h"

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;

int64_t VolumeInside(const CellBox& box, const CellBox& area) {
  return box.Overlaps(area) ? Volume(box.Intersection(area)) : 0;
}

}  // namespace

std::vector<RoomOverlap> OverlapsBetween(const RoomGraph& before, const RoomGraph& after) {
  std::map<std::pair<RoomId, RoomId>, int64_t> cells;
  for (const auto& [key, chunk] : after.space.AllChunks()) {
    for (uint32_t i = 0; i < chunk->leaves.size(); ++i) {
      const CellBox& leaf = chunk->leaves[i];
      const RoomId room = after.RoomOf({.chunk = key, .leaf = i});
      for (const LeafRef& old : before.space.LeavesIn(leaf)) {
        cells[{before.RoomOf(old), room}] += Volume(before.space.Leaf(old).Intersection(leaf));
      }
      if (const int64_t outside = Volume(leaf) - VolumeInside(leaf, before.space.Area()); outside > 0) {
        cells[{kVacuumRoom, room}] += outside;
      }
    }
  }
  for (const auto& [key, chunk] : before.space.AllChunks()) {
    for (uint32_t i = 0; i < chunk->leaves.size(); ++i) {
      const CellBox& leaf = chunk->leaves[i];
      if (const int64_t outside = Volume(leaf) - VolumeInside(leaf, after.space.Area()); outside > 0) {
        cells[{before.RoomOf({.chunk = key, .leaf = i}), kVacuumRoom}] += outside;
      }
    }
  }
  std::vector<RoomOverlap> overlaps;
  for (const auto& [pair, count] : cells) {
    overlaps.push_back({.before = pair.first, .after = pair.second, .cells = count});
  }
  return overlaps;
}

}  // namespace z13::station::rooms
