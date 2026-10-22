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

#include "topology_sources.h"

#include <lib_profile/profile.h>
#include <primitives/placement.h>
#include <rooms/room_sources.h>
#include <rooms/topology_fingerprint.h>

namespace z13::station {

TopologySources GatherSources(
    const flecs::query<const Block>& blocks, const z13::building::primitives::BlockPalette& palette) {
  Z13_PROFILE_ZONE("GatherSources");
  TopologySources gathered;
  blocks.each([&](const Block& block) {
    if (rooms::AddBlockSources(block, palette.palette, gathered.sources)) {
      gathered.fingerprint += rooms::BlockFingerprint(block);
    }
  });
  return gathered;
}

// The fingerprint is a sum, so a change adds or subtracts its block's.
TopologyEdits EditsOf(std::span<const BlockChange> changes, const z13::building::primitives::BlockPalette& palette) {
  TopologyEdits edits;
  for (const BlockChange& change : changes) {
    const bool added = change.kind == BlockChangeKind::kAdded;
    if (rooms::AddBlockSources(change.block, palette.palette, added ? edits.edits.added : edits.edits.removed)) {
      const uint64_t fingerprint = rooms::BlockFingerprint(change.block);
      if (added) {
        edits.fingerprint_change += fingerprint;
      } else {
        edits.fingerprint_change -= fingerprint;
      }
    }
  }
  return edits;
}

rooms::RoomSettings RoomSettingsOf(const z13::BuildingTuning& tuning) {
  return {.chunk_cells = tuning.room_chunk_cells, .max_opening_cells = tuning.room_max_opening_cells};
}

}  // namespace z13::station
