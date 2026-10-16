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
#include <unordered_map>
#include <vector>

#include <flecs.h>

#include <z13/components/block_changes.h>
#include <z13/components/block.h>

namespace z13::station {

struct BlockChangeBatch {
  // Derive from every block instead of `changes`.
  bool resync {};
  std::vector<BlockChange> changes;
};

// A block added and removed within one batch changed nothing.
inline void CancelPairs(std::vector<BlockChange>& changes) {
  std::unordered_map<flecs::entity_t, size_t> added;
  std::vector<bool> cancelled(changes.size());
  for (size_t i = 0; i < changes.size(); ++i) {
    if (changes[i].kind == BlockChangeKind::kAdded) {
      added[changes[i].entity] = i;
    } else if (const auto it = added.find(changes[i].entity); it != added.end()) {
      cancelled[i] = true;
      cancelled[it->second] = true;
      added.erase(it);
    }
  }
  size_t index {};
  std::erase_if(changes, [&cancelled, &index](const BlockChange&) { return cancelled[index++]; });
}

// Reads the block changes for one deriving system, once per tick, after the systems that
// build and destroy and before ClearBlockChangesPhase. It asks for a resync after a restore
// that changed blocks, a tick it missed, or a block count the changes don't explain. A restore
// sets the clock back to its snapshot (a rollback), so the tick after it isn't a missed one.
class BlockChangeReader {
 public:
  explicit BlockChangeReader(flecs::world world)
      : blocks_(world.query_builder<const Block>().detect_changes().build()),
        changes_(world.query<const BlockChange>()) {}

  // `known_blocks`: how many blocks the caller derives from, when it counts them itself.
  BlockChangeBatch Read(uint64_t restores, uint64_t tick, std::optional<size_t> known_blocks = std::nullopt) {
    BlockChangeBatch batch;
    changes_.each([&batch](const BlockChange& change) { batch.changes.push_back(change); });
    const auto added = std::ranges::count(batch.changes, BlockChangeKind::kAdded, &BlockChange::kind);
    const auto removed = static_cast<int64_t>(batch.changes.size()) - added;
    const int64_t known = known_blocks ? static_cast<int64_t>(*known_blocks) : count_;
    const bool restore = restores_seen_ != restores;
    restores_seen_ = restores;
    const bool blocks_changed = BlocksChanged();
    const bool restored = restore && blocks_changed;
    const bool missed_tick = !tick_ || (!restore && *tick_ + 1 != tick);
    count_ = blocks_.count();
    tick_ = tick;
    batch.resync = restored || missed_tick || known + added - removed != count_;
    if (batch.resync) {
      batch.changes.clear();
    } else {
      CancelPairs(batch.changes);
    }
    return batch;
  }

  const flecs::query<const Block>& Blocks() const { return blocks_; }

 private:
  // A restore that left the blocks' table alone changed no block. The walk resets changed().
  bool BlocksChanged() {
    const bool changed = blocks_.changed();
    blocks_.run([](flecs::iter& it) {
      while (it.next()) {
      }
    });
    return changed;
  }

  flecs::query<const Block> blocks_;
  flecs::query<const BlockChange> changes_;
  std::optional<uint64_t> restores_seen_;
  std::optional<uint64_t> tick_;
  int64_t count_ {};
};

}  // namespace z13::station
