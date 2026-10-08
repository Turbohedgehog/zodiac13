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


#include "block_watch.h"

#include <z13_settings/building_tuning.h>

namespace z13::building {

namespace {

template <typename T>
int WalkAndCount(flecs::query<T>& query) {
  int count {};
  query.run([&count](flecs::iter& it) {
    while (it.next()) {
      count += static_cast<int>(it.count());
    }
  });
  return count;
}

}  // namespace

BlockWatch::BlockWatch(flecs::world world)
    : world_(world),
      blocks_(world.query_builder<const z13::station::Block>("BlockWatch::Blocks").detect_changes().build()),
      spawn_points_(
          world.query_builder<const z13::station::SpawnPoint>("BlockWatch::SpawnPoints").detect_changes().build()) {}

bool BlockWatch::Unchanged() {
  // Before the walk, which resets it (count() doesn't).
  const bool changed = blocks_.changed() || spawn_points_.changed();
  const BlockWatchSeen seen {
      .blocks = WalkAndCount(blocks_),
      .spawn_points = WalkAndCount(spawn_points_),
      .chunk_cells = world_.get<z13::BuildingTuning>().index_chunk_cells,
  };
  const bool unchanged = !changed && last_ == seen;
  last_ = seen;
  return unchanged;
}

}  // namespace z13::building
