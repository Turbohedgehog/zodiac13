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

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <z13/components/rooms.h>
#include <rooms/room_cache.h>

#include "room_outline.h"

namespace z13::station {

// What the overlay draws for a room: its merged boxes, or the edges of its walls where oblique walls bound it.
struct RoomOverlayShape {
  std::vector<OverlayBox> boxes;
  std::vector<OverlaySegment> segments;
};

// Each room's overlay shape, made once per room and dropped when it changes. Derived, never state.
class RoomBoxCache {
 public:
  using Singleton = void;

  const RoomOverlayShape& ShapeOf(
      const z13::station::rooms::RoomCache& cache, z13::station::rooms::RoomId room, const WallSource& source);

 private:
  // RoomCache::Switches() when the boxes were last brought up to date.
  std::optional<uint64_t> switches_;
  std::unordered_map<z13::station::rooms::RoomId, RoomOverlayShape> shapes_;
};

}  // namespace z13::station
