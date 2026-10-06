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

#include "construction_site_system.h"

#include <string>

#include <flecs.h>
#include <Eigen/Dense>

#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/station.h>
#include <z13_primitives/palette.h>
#include <z13_settings/building_tuning.h>

#include "block_entities.h"

namespace z13::station {

namespace {

// assets/station/palette.json
constexpr uint32_t kFloorPrimitiveId = 1;
constexpr uint32_t kSpawnPointPrimitiveId = 8;

// A 6 m floor with its top at z = 0, the spawn marker in its middle.
constexpr int kFloorHalfWidthCells = 12;
constexpr int kSpawnMarkerHalfWidthCells = 2;

void PopulateConstructionSite(flecs::entity e, z13::gameplay::PopulateSceneEvent) {
  flecs::world world = e.world();
  if (!world.has<StationMode>()) {
    return;
  }
  const auto* palette = world.try_get<z13::building::primitives::BlockPalette>();
  if (palette == nullptr) {
    log_error("station: no block palette to build the construction site from");
    return;
  }
  const auto& tuning = world.get<BuildingTuning>();
  // Observers defer commands; SpawnPlayer, right after this event, must see the spawn point.
  const z13::ImmediateScope immediate(world);

  CreateBlock(world, std::string {"StationFloor"},
              {.spec = {.type_id = kFloorPrimitiveId, .size = {2 * kFloorHalfWidthCells, 2 * kFloorHalfWidthCells, 1}},
               .cell = {-kFloorHalfWidthCells, -kFloorHalfWidthCells, -1}},
              palette->palette, tuning);
  CreateBlock(world, std::string {"StationSpawnPoint"},
              {.spec = {.type_id = kSpawnPointPrimitiveId,
                        .size = {2 * kSpawnMarkerHalfWidthCells, 2 * kSpawnMarkerHalfWidthCells, 1}},
               .cell = {-kSpawnMarkerHalfWidthCells, -kSpawnMarkerHalfWidthCells, 0}},
              palette->palette, tuning);
}

void RegisterSystems(flecs::world world) {
  world.observer<z13::gameplay::PopulateSceneEvent>("ConstructionSiteSystem::PopulateConstructionSite")
      .event<z13::gameplay::PopulateSceneEvent>()
      .each(PopulateConstructionSite);
}

}  // namespace

void ConstructionSiteSystem::Register(flecs::world& world) {
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
