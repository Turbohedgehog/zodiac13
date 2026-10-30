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
#include <functional>
#include <map>

#include <flecs.h>

namespace z13::flecs_tools {

using PlayerBoundVisitor = std::function<void(flecs::entity, uint32_t player_id)>;

// The PlayerBound components by id, each walking its entities (RegisterComponent fills it).
struct PlayerBoundComponents {
  using Singleton = void;
  std::map<flecs::entity_t, std::function<void(flecs::world, const PlayerBoundVisitor&)>> each;
};

// Every entity a PlayerBound component ties to a player.
void EachPlayerBound(flecs::world world, const PlayerBoundVisitor& visit);

void RegisterPlayerBound(flecs::world& world);

}  // namespace z13::flecs_tools
