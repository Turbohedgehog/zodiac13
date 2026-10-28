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

#include <flecs.h>

#include <z13/components/spawn_stamp.h>
#include <z13/components/wall.h>

namespace z13::testing {

struct TestSpawnNamer {
  using Singleton = void;
  z13::gameplay::SpawnNamer namer {{.tick = 1, .player_id = 1}};
};

// The one namer for everything a test spawns into `world`, so that the ids differ.
inline z13::gameplay::SpawnNamer& TestNamer(flecs::world world) {
  return world.ensure<TestSpawnNamer>().namer;
}

// The id of the wall segment a test laid (the last, when it laid several).
inline z13::station::WallId LastSegmentId(flecs::world world) {
  z13::station::WallId id {};
  world.query<const z13::station::WallSegment>().each(
      [&id](const z13::station::WallSegment& segment) { id = segment.id; });
  return id;
}

}  // namespace z13::testing
