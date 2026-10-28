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

#include <lib_core/state/state_markers.h>

#include <z13/components/block.h>
#include <z13/components/state_changes.h>

namespace z13::station {

// Clears the block changes at the end of the tick; every module that reads them declares
// its reading phase before this one.
struct ClearBlockChangesPhase {};

enum class BlockChangeKind : uint8_t { kAdded, kRemoved };

// A block built or destroyed, on an entity of its own until ClearBlockChangesPhase. Not
// state: a replayed tick makes it again, and restores are told by WorldRestores.
struct BlockChange {
  BlockChangeKind kind {};
  flecs::entity_t entity {};
  Block block;
};

// Every block enters and leaves the world through these two. `block` is the entity's
// Block, passed in: a block spawned earlier in the same deferred system has none yet.
inline flecs::entity SpawnBlock(flecs::entity entity, const Block& block) {
  entity.add<z13::flecs_tools::StateEntity>().set(block);
  entity.world().entity().set(BlockChange {.kind = BlockChangeKind::kAdded, .entity = entity.id(), .block = block});
  z13::gameplay::RecordStateChange(entity, z13::gameplay::StateChangeKind::kSet, block.cell);
  return entity;
}

inline void DestroyBlock(flecs::entity entity, const Block& block) {
  entity.world().entity().set(BlockChange {.kind = BlockChangeKind::kRemoved, .entity = entity.id(), .block = block});
  z13::gameplay::RecordStateChange(entity, z13::gameplay::StateChangeKind::kRemoved, block.cell);
  entity.destruct();
}

}  // namespace z13::station
