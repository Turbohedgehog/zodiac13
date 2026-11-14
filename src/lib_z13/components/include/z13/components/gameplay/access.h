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

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <Eigen/Dense>
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

// A requirement on whatever block stands at `cell` of `grid`, from a blueprint: wall panels are derived and replaced,
// so the door they make can't hold the requirement itself.
struct AccessLock {
  using State = void;
  uint32_t grid {};
  Eigen::Vector3i cell {Eigen::Vector3i::Zero()};
  AccessSet required_all {};
  AccessSet required_any {};
};

constexpr std::string_view AccessName(Access access) {
  constexpr std::array kNames {"bridge", "armory", "brig", "security", "engineering", "atmospherics", "medical",
                               "science", "cargo", "tech_tunnels", "eva"};
  return kNames[static_cast<uint8_t>(access)];
}

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

inline std::optional<Access> ParseAccess(std::string_view name) {
  for (uint8_t i = 0; i <= static_cast<uint8_t>(Access::kEva); ++i) {
    if (AccessName(static_cast<Access>(i)) == name) {
      return static_cast<Access>(i);
    }
  }
  return std::nullopt;
}

// Whether `player` may use what `target` is, given the `locks` on the cells it stands at: a block without a
// requirement is open.
inline bool MayUse(const flecs::world& world, flecs::entity_t player, flecs::entity_t target,
                   std::span<const AccessLock> locks = {}) {
  if (!world.is_alive(target) || !world.is_alive(player)) {
    return true;
  }
  const auto* requirement = world.entity(target).try_get<AccessRequirement>();
  if (requirement == nullptr && locks.empty()) {
    return true;
  }
  const auto* owner = world.entity(player).try_get<z13::gameplay::Player>();
  if (owner == nullptr) {
    return false;
  }
  const AccessSet held = HeldAccess(world, owner->id);
  return (requirement == nullptr || Covers(held, *requirement)) &&
         std::ranges::all_of(locks, [held](const AccessLock& lock) {
           return Covers(held, {.required_all = lock.required_all, .required_any = lock.required_any});
         });
}

}  // namespace z13::inventory
