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
#include <vector>

#include <flecs.h>

#include <lib_core/utils/status.h>

#include <z13/components/station.h>

#include "build_run.h"
#include "build_validation.h"

namespace z13::building {

// What taking a box of cells out of the station does: the blocks it touches go, and what
// of them lies outside the box comes back as new blocks.
struct CutPlan {
  std::vector<flecs::entity> removed;
  std::vector<z13::station::Block> remainders;
  int spawn_points_removed {};
};

// A box to cut out of a station that has `spawn_points` spawn points, `spawn_points_added`
// of which the cut's own block brings back.
struct CutRequest {
  z13::building::primitives::CellBox cells;
  int spawn_points {};
  int spawn_points_added {};
};

// The plan for the cut, or why not: the spawn points left after it must not drop below one.
// Blocks are taken in cell order, not entity order, so every peer plans the same remainders.
std::expected<CutPlan, std::string> PlanCut(
    flecs::world world, const BuildView& view, const MadeBlocks& made, const CutRequest& request);

// Removes the planned blocks and creates the remainders, in the index as well.
void ApplyCut(flecs::world world, const CutPlan& plan, BuildRun& run);

}  // namespace z13::building
