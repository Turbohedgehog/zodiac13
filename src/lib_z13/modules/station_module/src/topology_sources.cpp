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

#include <z13_primitives/placement.h>
#include <z13_rooms/portal_source.h>
#include <z13_rooms/topology_fingerprint.h>

namespace z13::station {

TopologySources GatherSources(
    const flecs::query<const Block>& blocks, const z13::building::primitives::BlockPalette& palette) {
  TopologySources gathered;
  blocks.each([&](const Block& block) {
    const auto primitive = palette.palette.Find(block.spec.type_id);
    if (!primitive || !rooms::IsSealing(*primitive)) {
      return;
    }
    gathered.fingerprint += rooms::BlockFingerprint(block);
    gathered.sources.sealed.push_back(z13::building::primitives::OccupiedCells(block));
    if (const auto portal = rooms::PortalOf(block, *primitive)) {
      gathered.sources.portals.push_back(*portal);
    }
  });
  return gathered;
}

}  // namespace z13::station
