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
#include <cstdint>
#include <optional>
#include <vector>

#include <flecs.h>

#include <z13/components/block_changes.h>
#include <z13/components/station.h>

namespace z13::station {

// What a system deriving from the blocks has to do since its previous read.
struct BlockChangeBatch {
  // The blocks changed past the change events (a restore, a block set directly): derive
  // from every block instead of `changes`.
  bool resync {};
  std::vector<BlockChange> changes;
};

// One per deriving system, read once per tick after the systems that build and destroy and
// before ClearBlockChangesPhase.
class BlockChangeReader {
 public:
  explicit BlockChangeReader(flecs::world world)
      : blocks_(world.query<const Block>()), changes_(world.query<const BlockChange>()) {}

  // `known_blocks`: how many blocks the caller derived from so far; a count the changes
  // don't account for is a change past them.
  BlockChangeBatch Read(uint64_t restores, size_t known_blocks) {
    BlockChangeBatch batch;
    changes_.each([&batch](const BlockChange& change) { batch.changes.push_back(change); });
    const auto added = std::ranges::count(batch.changes, BlockChangeKind::kAdded, &BlockChange::kind);
    const auto removed = static_cast<int64_t>(batch.changes.size()) - added;
    const bool restored = restores_seen_ != restores;
    restores_seen_ = restores;
    batch.resync = restored || static_cast<int64_t>(known_blocks) + added - removed != blocks_.count();
    if (batch.resync) {
      batch.changes.clear();
    }
    return batch;
  }

  const flecs::query<const Block>& Blocks() const { return blocks_; }

 private:
  flecs::query<const Block> blocks_;
  flecs::query<const BlockChange> changes_;
  std::optional<uint64_t> restores_seen_;
};

}  // namespace z13::station
