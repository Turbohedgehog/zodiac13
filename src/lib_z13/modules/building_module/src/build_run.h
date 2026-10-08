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

#include <z13/components/gameplay.h>
#include <z13/components/station.h>
#include <z13_grid/block_index.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13_settings/building_tuning.h>

#include "build_validation.h"

namespace z13::building {

// The blocks made this tick, by entity: a deferred entity has no Block to read yet, so a
// later request in the same tick finds it here.
using MadeBlocks = std::unordered_map<flecs::entity_t, z13::station::Block>;

// What the build requests of one tick share, so each sees what the ones before it made.
struct BuildRun {
  std::reference_wrapper<grid::BlockIndex> index;
  std::reference_wrapper<z13::gameplay::IdCounters> counters;
  std::reference_wrapper<const z13::building::primitives::Palette> palette;
  std::reference_wrapper<const BuildingTuning> tuning;
  std::vector<PlayerSphere> players;
  std::vector<z13::building::primitives::CellBox> spawn_clearances;
  int spawn_points {};
  MadeBlocks made;

  BuildView View() const {
    return {.palette = palette, .index = std::cref(index.get()), .tuning = tuning, .players = players,
            .spawn_clearances = spawn_clearances};
  }
};

}  // namespace z13::building
