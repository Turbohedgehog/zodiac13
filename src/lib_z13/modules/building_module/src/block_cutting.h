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

#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

#include <flecs.h>

#include <lib_core/utils/status.h>

#include <z13/components/gameplay.h>
#include <z13/components/station.h>
#include <z13_grid/block_index.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>
#include <z13_settings/building_tuning.h>

namespace z13::building {

// What taking a box of cells out of the station does: the blocks it touches go, and what
// of them lies outside the box comes back as new blocks.
// The blocks made this tick, by entity: a deferred entity has no Block to read yet, so a
// later request in the same tick finds it here.
using MadeBlocks = std::unordered_map<flecs::entity_t, z13::station::Block>;

struct CutPlan {
  std::vector<flecs::entity> removed;
  std::vector<z13::station::Block> remainders;
  int spawn_points_removed {};
};

// The plan for cutting `cells`, or why not: the spawn points left after it, plus
// `spawn_points_added`, must not drop below one. Blocks are taken in cell order, not entity
// order, so every peer plans the same remainders.
std::expected<CutPlan, std::string> PlanCut(
    flecs::world world, const grid::BlockIndex& index, const z13::building::primitives::Palette& palette,
    const z13::building::primitives::CellBox& cells, int spawn_points, const MadeBlocks& made,
    int spawn_points_added = 0);

// Removes the planned blocks and creates the remainders, in the index as well.
void ApplyCut(
    flecs::world world, const CutPlan& plan, grid::BlockIndex& index, z13::gameplay::IdCounters& counters,
    const z13::building::primitives::Palette& palette, const BuildingTuning& tuning, MadeBlocks& made);

}  // namespace z13::building
