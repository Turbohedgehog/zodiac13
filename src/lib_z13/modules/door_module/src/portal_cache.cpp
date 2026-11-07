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


#include "portal_cache.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include <rooms/portal_source.h>

namespace z13::door_module {

namespace rooms = z13::station::rooms;

std::vector<z13::station::DoorOpening> PortalCache::OpenOf(
    const flecs::query<const z13::station::DoorState, const z13::station::Block>& doors,
    const z13::building::primitives::BlockPalette& palette) {
  std::unordered_map<flecs::entity_t, PortalEntry> seen;
  std::vector<z13::station::DoorOpening> open;
  // The openings are in the main grid's cells, which the renderer matches against its portals.
  doors.each([&](flecs::entity door, const z13::station::DoorState& state, const z13::station::Block& block) {
    const auto primitive = palette.palette.Find(block.spec.type_id);
    if (state.position == 0 || block.grid != z13::station::kMainGrid || !primitive) {
      return;
    }
    auto known = entries_.extract(door.id());
    PortalEntry entry = known && known.mapped().block == block ? std::move(known.mapped()) : PortalEntry {.block = block};
    if (entry.openings.empty()) {
      for (const rooms::PortalSource& source : rooms::PortalsOf(block, primitive->get(), palette.palette)) {
        entry.openings.push_back({.min = source.opening.min, .extent = source.opening.extent});
      }
    }
    open.insert(open.end(), entry.openings.begin(), entry.openings.end());
    seen.emplace(door.id(), std::move(entry));
  });
  entries_ = std::move(seen);
  std::ranges::sort(open, [](const z13::station::DoorOpening& a, const z13::station::DoorOpening& b) {
    return std::tie(a.min.x(), a.min.y(), a.min.z(), a.extent.x(), a.extent.y(), a.extent.z()) <
           std::tie(b.min.x(), b.min.y(), b.min.z(), b.extent.x(), b.extent.y(), b.extent.z());
  });
  return open;
}

}  // namespace z13::door_module
