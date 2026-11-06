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
#include <unordered_map>
#include <vector>

#include <flecs.h>

#include <z13/components/station/block.h>
#include <z13/components/station/spawn_stamp.h>
#include <grid/block_index.h>
#include <primitives/palette_fwd.h>
#include <primitives/cell_box.h>
#include <z13_settings/building_tuning.h>

#include "build_grids.h"
#include "build_validation.h"

namespace z13::building {

// The blocks made this tick, by entity: a deferred entity has no Block to read yet, so a
// later request in the same tick finds it here.
using MadeBlocks = std::unordered_map<flecs::entity_t, z13::station::Block>;

// What the build requests of one tick share, so each sees what the ones before it made.
struct BuildRun {
  // The grid the request in hand builds in (Select).
  std::reference_wrapper<grid::BlockIndex> index;
  z13::station::GridId grid = z13::station::kMainGrid;
  bool selected {};
  std::reference_wrapper<const z13::building::primitives::Palette> palette;
  std::reference_wrapper<const BuildingTuning> tuning;
  std::vector<PlayerSphere> players;  // in the world, meters
  std::vector<PlayerSphere> local_players;
  // The space above the spawn points of each grid, in its cells; those of the selected one are in `spawn_clearances`.
  std::unordered_map<z13::station::GridId, std::vector<z13::building::primitives::CellBox>> clearances_by_grid;
  std::vector<z13::building::primitives::CellBox> spawn_clearances;
  int spawn_points {};
  MadeBlocks made;
  // By player: what each spawns this tick is numbered in turn, across every request of the run.
  std::unordered_map<uint32_t, z13::gameplay::SpawnNamer> namers;

  z13::gameplay::SpawnNamer& NamerOf(const z13::gameplay::SpawnStamp& stamp) {
    return namers.try_emplace(stamp.player_id, stamp).first->second;
  }

  void Select(const GridView& target) {
    if (selected) {
      clearances_by_grid[grid] = std::move(spawn_clearances);
    }
    selected = true;
    grid = target.id;
    index = target.MutableIndex();
    spawn_clearances = std::move(clearances_by_grid[grid]);
    local_players.clear();
    for (const PlayerSphere& player : players) {
      local_players.push_back({.center = target.ToLocalMeters(player.center),
                               .radius = player.radius});
    }
  }

  BuildView View() const {
    return {.palette = palette, .index = std::cref(index.get()), .tuning = tuning, .players = local_players,
            .spawn_clearances = spawn_clearances};
  }
};

}  // namespace z13::building
