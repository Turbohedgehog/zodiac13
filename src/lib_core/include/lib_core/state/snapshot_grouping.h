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
#include <memory>
#include <optional>

#include <flecs.h>

#include <lib_core/state/world_serializer.h>

namespace z13::flecs_tools {

// Set by a module whose state entities are many and rarely change (the station's blocks).
// CaptureState files them by group: a group whose fingerprint equals the previous capture's
// reuses that snapshot instead of encoding its entities again, and RestoreWorld leaves a
// group alone when the world already matches. `hash_of` must cover the entity's name and
// every state value it has; a 64-bit fingerprint is trusted not to collide.
struct SnapshotGrouping {
  using Singleton = void;
  std::optional<flecs::entity_t> member;  // a component every grouped entity has
  std::function<std::optional<SnapshotGroupId>(flecs::entity)> group_of;
  std::function<uint64_t(flecs::entity)> hash_of;
  // Optional. Whether no grouped entity changed since its previous call, which CaptureState
  // makes once per capture; lets a capture skip fingerprinting every entity.
  std::function<bool()> unchanged;
  std::map<SnapshotGroupId, std::shared_ptr<const GroupSnapshot>> captured;  // by the last CaptureState
};

// The splitmix64 finalizer: spreads `value` over all bits, for building hash_of.
uint64_t MixHash(uint64_t value);

void RegisterSnapshotGrouping(flecs::world& world);

}  // namespace z13::flecs_tools
