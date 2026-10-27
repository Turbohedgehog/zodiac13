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
#include <unordered_set>

#include <flecs.h>

#include <lib_core/state/world_serializer.h>
#include <sectors/interest_map.h>
#include <sectors/sector.h>

namespace z13::net {

using SectorSet = std::unordered_set<sectors::SectorKey, sectors::SectorHash>;

// What one client has of the world (docs/interest-design.md): the sectors it is subscribed to, the
// ring around them where a player's reach can touch, and the players in that ring, itself included.
struct InterestView {
  SectorSet sectors;
  SectorSet presence;
  std::unordered_set<uint32_t> players;
};

SectorSet Dilated(const SectorSet& sectors);

// What a client joining at `center` will have: the view before its player is in the sector index.
InterestView ViewAround(flecs::world world, sectors::ClientId client, const sectors::SectorKey& center);

// Nothing while the client has no interest yet (its player is not in the sector index).
std::optional<InterestView> ViewOf(flecs::world world, sectors::ClientId client);

// The snapshot of the view: the players in it and the block groups in its sectors, and what belongs to none.
z13::flecs_tools::WorldSnapshot FilterSnapshot(
    flecs::world world, const z13::flecs_tools::WorldSnapshot& snapshot, const InterestView& view);

}  // namespace z13::net
