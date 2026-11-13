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

#include <flecs.h>

#include <z13/components/gameplay/inventory.h>
#include <z13/components/gameplay/player.h>

// Access by passes, after SS13 (docs/station-systems-plan.md, "Доступ"): a pass is an item whose set of accesses
// a reader at a door compares with the door's requirement; there is no central server, so no revoking.
namespace z13::inventory {

// The zones of the station a pass can open.
enum class Access : uint8_t {
  kBridge,
  kArmory,
  kBrig,
  kSecurity,
  kEngineering,
  kAtmospherics,
  kMedical,
  kScience,
  kCargo,
  kTechTunnels,
  kEva,
};

using AccessSet = uint32_t;

constexpr AccessSet AccessBit(Access access) {
  return AccessSet {1} << static_cast<uint8_t>(access);
}

// On an item of ItemKind::kPass.
struct Pass {
  using State = void;
  AccessSet access {};
};

// On a block that checks passes: all of `required_all` and one of `required_any` (when it names any) must be held; an
// empty requirement is open. Not `all_of`/`any_of`: those field names crash flecs meta registration at teardown.
struct AccessRequirement {
  using State = void;
  AccessSet required_all {};
  AccessSet required_any {};
};

constexpr bool Covers(AccessSet held, const AccessRequirement& requirement) {
  return (held & requirement.required_all) == requirement.required_all &&
         (requirement.required_any == 0 || (held & requirement.required_any) != 0);
}

// The accesses of the passes in `player_id`'s hotbar cells; a pass elsewhere (a container, later) doesn't count.
inline AccessSet HeldAccess(const flecs::world& world, uint32_t player_id) {
  AccessSet held {};
  world.each([&](const CarriedBy& carried, const Pass& pass) {
    if (carried.player_id == player_id && carried.cell < kHotbarCells) {
      held |= pass.access;
    }
  });
  return held;
}

// Whether `player` may use what `target` is: a block without a requirement is open.
inline bool MayUse(const flecs::world& world, flecs::entity_t player, flecs::entity_t target) {
  if (!world.is_alive(target) || !world.is_alive(player)) {
    return true;
  }
  const auto* requirement = world.entity(target).try_get<AccessRequirement>();
  const auto* owner = world.entity(player).try_get<z13::gameplay::Player>();
  return requirement == nullptr || (owner != nullptr && Covers(HeldAccess(world, owner->id), *requirement));
}

}  // namespace z13::inventory
