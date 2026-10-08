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

#include <flecs.h>

#include <z13/components/station.h>

namespace z13::building {

// What BlockWatch saw last; a changed count catches a removal change detection misses.
struct BlockWatchSeen {
  int blocks {};
  int spawn_points {};
  int chunk_cells {};

  bool operator==(const BlockWatchSeen&) const = default;
};

// Tells whether any block's snapshot could differ since the previous call
// (SnapshotGrouping::unchanged).
class BlockWatch {
 public:
  explicit BlockWatch(flecs::world world);

  bool Unchanged();

 private:
  flecs::world world_;
  flecs::query<const z13::station::Block> blocks_;
  flecs::query<const z13::station::SpawnPoint> spawn_points_;
  std::optional<BlockWatchSeen> last_;
};

}  // namespace z13::building
