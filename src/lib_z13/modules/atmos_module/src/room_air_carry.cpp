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

#include "room_air_carry.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <unordered_map>

#include <rooms/room_overlap.h>

namespace z13::atmos_module {

namespace {

using z13::station::rooms::RoomGraph;
using z13::station::rooms::RoomId;
using z13::utility::RoomAirEntry;
using z13::utility::atmos::Air;
using z13::utility::atmos::Gas;

std::optional<std::reference_wrapper<const RoomAirEntry>> EntryAt(const std::vector<RoomAirEntry>& air,
                                                                  const Eigen::Vector3i& anchor) {
  const auto found = std::ranges::find(air, anchor, &RoomAirEntry::anchor);
  if (found == air.end()) {
    return std::nullopt;
  }
  return std::cref(*found);
}

}  // namespace

Air AirOf(const RoomAirEntry& entry) {
  Air air {.energy = entry.energy};
  air.millimoles[static_cast<size_t>(Gas::kOxygen)] = entry.oxygen;
  air.millimoles[static_cast<size_t>(Gas::kNitrogen)] = entry.nitrogen;
  air.millimoles[static_cast<size_t>(Gas::kCarbonDioxide)] = entry.carbon_dioxide;
  return air;
}

RoomAirEntry EntryOf(const Eigen::Vector3i& anchor, const Air& air) {
  return {.anchor = anchor,
          .oxygen = air.millimoles[static_cast<size_t>(Gas::kOxygen)],
          .nitrogen = air.millimoles[static_cast<size_t>(Gas::kNitrogen)],
          .carbon_dioxide = air.millimoles[static_cast<size_t>(Gas::kCarbonDioxide)],
          .energy = air.energy};
}

std::vector<RoomAirEntry> SeedRooms(const RoomGraph& graph) {
  std::vector<RoomAirEntry> seeded;
  for (const RoomId id : graph.ByAnchor()) {
    const auto& room = graph.rooms.at(id);
    seeded.push_back(EntryOf(room.anchor, z13::utility::atmos::StandardAir(room.volume_cells)));
  }
  return seeded;
}

std::vector<RoomAirEntry> CarryAir(const std::vector<RoomAirEntry>& air, const RoomGraph& before,
                                   const RoomGraph& after) {
  std::unordered_map<RoomId, Air> arrived;
  const auto overlaps = z13::station::rooms::OverlapsBetween(before, after);
  for (auto first = overlaps.begin(); first != overlaps.end();) {
    const auto last = std::find_if(first, overlaps.end(), [&](const auto& o) { return o.before != first->before; });
    const auto old_room = before.rooms.find(first->before);
    const auto held = old_room != before.rooms.end() && first->before != z13::station::rooms::kVacuumRoom
                          ? EntryAt(air, old_room->second.anchor)
                          : std::nullopt;
    if (held) {
      std::vector<int64_t> weights;
      std::transform(first, last, std::back_inserter(weights), [](const auto& o) { return o.cells; });
      const std::vector<Air> shares = z13::utility::atmos::Divide(AirOf(held->get()), weights);
      for (size_t i = 0; i < shares.size(); ++i) {
        const RoomId target = (first + static_cast<std::ptrdiff_t>(i))->after;
        if (target != z13::station::rooms::kVacuumRoom) {
          arrived[target] += shares[i];
        }
      }
    }
    first = last;
  }
  std::vector<RoomAirEntry> carried;
  for (const RoomId id : after.ByAnchor()) {
    carried.push_back(EntryOf(after.rooms.at(id).anchor, arrived[id]));
  }
  return carried;
}

std::vector<RoomAirEntry> KeepAirByAnchor(const std::vector<RoomAirEntry>& air, const RoomGraph& after) {
  std::vector<RoomAirEntry> kept;
  for (const RoomId id : after.ByAnchor()) {
    const Eigen::Vector3i& anchor = after.rooms.at(id).anchor;
    const auto held = EntryAt(air, anchor);
    kept.push_back(held ? held->get() : EntryOf(anchor, Air {}));
  }
  return kept;
}

}  // namespace z13::atmos_module
