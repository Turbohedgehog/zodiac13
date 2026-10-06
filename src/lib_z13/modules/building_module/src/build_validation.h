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

#include <span>
#include <string>

#include <Eigen/Dense>

#include <z13/components/station.h>
#include <z13_grid/block_index.h>
#include <z13_primitives/palette.h>
#include <z13_settings/building_tuning.h>

#include <lib_core/utils/status.h>


// Only what would break the world is refused; dangerous builds (venting a room, cutting
// power) are allowed, see "Строительный инструмент" in docs/station-primitives-plan.md.
namespace z13::building {

struct PlayerSphere {
  Eigen::Vector3f center = Eigen::Vector3f::Zero();
  float radius {};
};

// A known primitive within its size limits, on free cells (or `replaces_cells`, a cut that
// takes them first), clear of every player and of the space above spawn points
// (`spawn_clearances`); a new spawn point needs that space free.
Status ValidateBuild(
    const z13::station::Block& block, const z13::building::primitives::Palette& palette, const grid::BlockIndex& index,
    std::span<const PlayerSphere> players, std::span<const z13::building::primitives::CellBox> spawn_clearances,
    const BuildingTuning& tuning, bool replaces_cells = false);

// The last spawn point stays, or new players would have nowhere to appear.
Status ValidateDestroy(
    const z13::station::Block& block, const z13::building::primitives::Palette& palette, int spawn_points);

}  // namespace z13::building
