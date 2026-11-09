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

#include <flecs.h>

#include <z13/components/station/wall.h>

namespace z13::building {

// What WallWatch saw last; counts catch a removal change detection misses, and a wall entity holding anything beyond
// its wall component (HashOfWallEntity) makes the watch report a change every time.
struct WallWatchSeen {
  int walls {};
  int plain_walls {};
  uint64_t restores {};

  bool operator==(const WallWatchSeen&) const = default;
};

// Tells whether any wall entity's snapshot could differ since the previous call
// (SnapshotGrouping::hashed_unchanged).
class WallWatch {
 public:
  explicit WallWatch(flecs::world world);

  bool Unchanged(flecs::world world);

 private:
  flecs::query<const z13::station::WallNode> nodes_;
  flecs::query<const z13::station::WallSegment> segments_;
  flecs::query<const z13::station::WallOpening> openings_;
  std::optional<WallWatchSeen> last_;
};

}  // namespace z13::building
