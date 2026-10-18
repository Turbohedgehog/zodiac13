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
#include <primitives/occupancy.h>
#include <primitives/placement.h>
#include <rooms/portal_source.h>
#include <rooms/topology_fingerprint.h>

namespace z13::station {

namespace {

// Adds a sealing block's sources to `sources`; false for a block that isn't one.
bool AddSources(const Block& block, const z13::building::primitives::BlockPalette& palette,
                rooms::RoomSources& sources) {
  const auto primitive = palette.palette.Find(block.spec.type_id);
  if (!primitive || !rooms::IsSealing(*primitive)) {
    return false;
  }
  const auto sealed = z13::building::primitives::SealedBoxes(block, palette.palette);
  sources.sealed.insert(sources.sealed.end(), sealed.begin(), sealed.end());
  if (const auto portal = rooms::PortalOf(block, *primitive)) {
    sources.portals.push_back(*portal);
  }
  return true;
}

}  // namespace

TopologySources GatherSources(
    const flecs::query<const Block>& blocks, const z13::building::primitives::BlockPalette& palette) {
  Z13_PROFILE_ZONE("GatherSources");
  TopologySources gathered;
  blocks.each([&](const Block& block) {
    if (AddSources(block, palette, gathered.sources)) {
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
    if (AddSources(change.block, palette, added ? edits.edits.added : edits.edits.removed)) {
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
