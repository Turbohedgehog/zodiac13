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

#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <flecs.h>

#include <primitives/palette_fwd.h>
#include <primitives/cell_box.h>
#include <z13/components/station/block.h>
#include <z13/components/station/door.h>

namespace z13::atmos_module {

using DoorQuery = flecs::query<const z13::station::DoorState, const z13::station::Block>;

// A door of one grid with the openings its block closes.
struct DoorAt {
  flecs::entity entity;
  std::vector<z13::building::primitives::CellBox> openings;
  z13::station::DoorPosition position {};
};

// Kept while the block is as it was: working the openings out was most of an atmosphere step.
struct DoorOpenings {
  z13::station::Block block;
  std::vector<z13::building::primitives::CellBox> openings;
};
using DoorOpeningsCache = std::unordered_map<flecs::entity_t, DoorOpenings>;

std::vector<DoorAt> DoorsOf(const DoorQuery& doors, const z13::building::primitives::BlockPalette& palette,
                            z13::station::GridId grid, DoorOpeningsCache& cache);

bool Overlaps(const DoorAt& door, const z13::building::primitives::CellBox& opening);

std::optional<std::reference_wrapper<const DoorAt>> DoorAtOpening(std::span<const DoorAt> doors,
                                                                 const z13::building::primitives::CellBox& opening);

}  // namespace z13::atmos_module
