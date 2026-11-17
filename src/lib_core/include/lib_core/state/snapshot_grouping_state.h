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
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <lib_core/state/state_watch.h>
#include <lib_core/state/world_serializer.h>

namespace z13::flecs_tools {

struct CachedEntity {
  std::optional<uint64_t> hash;  // none for an entity FingerprintOf can't cover
  TableKey table {};
  SharedEntity snapshot;
};

// What a table of ungrouped entities gave the last capture, taken as it was while the table is not written.
struct UngroupedTable {
  int count {};
  uint64_t seen {};  // the capture that last found it
  std::vector<flecs::entity_t> ids;
  SharedEntities snapshots;
};

// The order the last capture sorted its entities in, reused while their names come in the same order.
struct SortMemory {
  SharedEntities collected;
  std::vector<size_t> order;  // the collected index of each sorted entity
};

struct GroupedEntity {
  SnapshotGroupId group {};
  uint64_t fingerprint {};
  TableKey table {};
  uint64_t seen {};  // the update that last found it
};

struct GroupIndexEntry {
  uint64_t sum {};  // of their mixed fingerprints, so the order they are found in doesn't matter
  std::unordered_set<flecs::entity_t> members;
};

// What the last CaptureState made, kept to reuse.
struct SnapshotGroupingState {
  std::map<SnapshotGroupId, std::shared_ptr<const GroupSnapshot>> captured;
  std::map<SnapshotGroupId, uint64_t> captured_keys;  // each group's index key when it was encoded
  std::unordered_map<flecs::entity_t, CachedEntity> ungrouped;
  std::unordered_map<TableKey, UngroupedTable> ungrouped_tables;
  SortMemory ungrouped_order;
  std::unordered_map<flecs::entity_t, GroupedEntity> grouped;
  std::map<SnapshotGroupId, GroupIndexEntry> index;
  uint64_t update {};  // of the group index
  uint64_t ungrouped_update {};
  uint64_t layout_key {};  // of the GroupLayout the index was filed by
  uint64_t fingerprints_made {};  // grouped entities fingerprinted, for tests and benches
  StateWatch watch;
};

}  // namespace z13::flecs_tools
