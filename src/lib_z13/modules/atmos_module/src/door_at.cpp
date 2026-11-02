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

#include "door_at.h"

#include <algorithm>
#include <utility>

#include <rooms/portal_source.h>

namespace z13::atmos_module {

namespace {

using z13::building::primitives::BlockPalette;
using z13::building::primitives::CellBox;
using z13::station::Block;
using z13::station::DoorState;
using z13::station::kMainGrid;

}  // namespace

std::vector<DoorAt> DoorsOf(const DoorQuery& doors, const BlockPalette& palette) {
  std::vector<DoorAt> found;
  doors.each([&](flecs::entity entity, const DoorState& state, const Block& block) {
    const auto primitive = block.grid == kMainGrid ? palette.palette.Find(block.spec.type_id) : std::nullopt;
    if (!primitive) {
      return;
    }
    DoorAt door {.entity = entity, .position = state.position};
    for (const auto& source : z13::station::rooms::PortalsOf(block, primitive->get(), palette.palette)) {
      door.openings.push_back(source.opening);
    }
    found.push_back(std::move(door));
  });
  return found;
}

bool Overlaps(const DoorAt& door, const CellBox& opening) {
  return std::ranges::any_of(door.openings, [&opening](const CellBox& at) { return at.Overlaps(opening); });
}

std::optional<std::reference_wrapper<const DoorAt>> DoorAtOpening(std::span<const DoorAt> doors,
                                                                 const CellBox& opening) {
  const auto found = std::ranges::find_if(doors, [&opening](const DoorAt& at) { return Overlaps(at, opening); });
  if (found == doors.end()) {
    return std::nullopt;
  }
  return std::cref(*found);
}

}  // namespace z13::atmos_module
