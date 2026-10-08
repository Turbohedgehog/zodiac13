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
#include <memory>
#include <optional>
#include <string_view>

#include <flecs.h>

#include <lib_core/state/snapshot_grouping_state.h>
#include <lib_core/state/world_serializer.h>

namespace z13::flecs_tools {

using GroupOf = std::function<SnapshotGroupId(flecs::entity)>;

// Files many rarely changing state entities (the station's blocks) into snapshot groups.
// `hash_of` must cover the entity's name and every state value it has; a 64-bit
// fingerprint is trusted not to collide.
struct SnapshotGrouping {
  using Singleton = void;
  std::optional<flecs::entity_t> member;  // a component every grouped entity has
  // Called once per scan; what it returns files each member entity.
  std::function<GroupOf()> make_group_of;
  std::function<uint64_t(flecs::entity)> hash_of;
  // Optional: no grouped entity changed since the previous call, so fingerprinting is skipped.
  std::function<bool()> unchanged;
  std::shared_ptr<SnapshotGroupingState> state_ = std::make_shared<SnapshotGroupingState>();
};

using OptionalGrouping = std::optional<std::reference_wrapper<const SnapshotGrouping>>;

// The splitmix64 finalizer.
uint64_t MixHash(uint64_t value);

// FNV-1a: the same on every platform, unlike std::hash.
uint64_t HashName(std::string_view name);

void RegisterSnapshotGrouping(flecs::world& world);

}  // namespace z13::flecs_tools
