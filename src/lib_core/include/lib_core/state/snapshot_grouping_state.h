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
#include <map>
#include <memory>
#include <unordered_map>

#include <lib_core/state/world_serializer.h>

namespace z13::flecs_tools {

struct CachedEntity {
  uint64_t hash {};
  EntitySnapshot snapshot;
};

// What the last CaptureState made, kept to reuse: block groups and fingerprinted ungrouped entities.
struct SnapshotGroupingState {
  std::map<SnapshotGroupId, std::shared_ptr<const GroupSnapshot>> captured;
  std::unordered_map<flecs::entity_t, CachedEntity> ungrouped;
};

}  // namespace z13::flecs_tools
