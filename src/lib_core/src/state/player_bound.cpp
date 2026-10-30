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

#include <lib_core/state/player_bound.h>

#include <lib_core/state/world_state.h>

namespace z13::flecs_tools {

void EachPlayerBound(flecs::world world, const PlayerBoundVisitor& visit) {
  for (const auto& [component, each] : world.get<PlayerBoundComponents>().each) {
    each(world, visit);
  }
}

void RegisterPlayerBound(flecs::world& world) {
  RegisterComponent<PlayerBoundComponents>(world);
  world.set<PlayerBoundComponents>({});
}

}  // namespace z13::flecs_tools
