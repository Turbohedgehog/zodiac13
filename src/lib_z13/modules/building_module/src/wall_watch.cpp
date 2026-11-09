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

#include "wall_watch.h"

#include <lib_core/state/world_serializer.h>

namespace z13::building {

namespace {

// Wall entities with their name, StateEntity and one wall component: the type HashOfWallEntity covers.
constexpr int kPlainWallTypeSize = 3;

template <typename T>
void Count(flecs::query<T>& query, WallWatchSeen& seen) {
  query.run([&seen](flecs::iter& it) {
    while (it.next()) {
      const int count = static_cast<int>(it.count());
      seen.walls += count;
      if (it.table().type().count() == kPlainWallTypeSize) {
        seen.plain_walls += count;
      }
    }
  });
}

}  // namespace

WallWatch::WallWatch(flecs::world world)
    : nodes_(world.query_builder<const z13::station::WallNode>("WallWatch::Nodes").detect_changes().build()),
      segments_(world.query_builder<const z13::station::WallSegment>("WallWatch::Segments").detect_changes().build()),
      openings_(world.query_builder<const z13::station::WallOpening>("WallWatch::Openings").detect_changes().build()) {}

bool WallWatch::Unchanged(flecs::world world) {
  // Before the walk, which resets it (count() doesn't).
  const bool changed = nodes_.changed() || segments_.changed() || openings_.changed();
  const auto* restores = world.try_get<z13::flecs_tools::WorldRestores>();  // bare test worlds have none
  WallWatchSeen seen {.restores = restores != nullptr ? restores->count : 0};
  Count(nodes_, seen);
  Count(segments_, seen);
  Count(openings_, seen);
  const bool unchanged = !changed && seen.walls == seen.plain_walls && last_ == seen;
  last_ = seen;
  return unchanged;
}

}  // namespace z13::building
