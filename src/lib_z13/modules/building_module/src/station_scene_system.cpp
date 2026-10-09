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

#include "station_scene_system.h"

#include <filesystem>
#include <format>
#include <string>

#include <flecs.h>

#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/file_io.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/station.h>
#include <primitives/blueprint.h>
#include <primitives/palette.h>
#include <primitives/station_assets.h>
#include <z13_settings/building_tuning.h>

#include "block_entities.h"
#include "build_validation.h"
#include "asset_file.h"

namespace z13::building {

namespace {

using z13::building::primitives::BlockPalette;
using z13::station::StationMode;
using z13::station::StationSceneChoice;

void PopulateStationScene(
    flecs::entity e,
    z13::gameplay::PopulateSceneEvent,
    const StationSceneChoice& choice,
    const BlockPalette& palette,
    const BuildingTuning& tuning) {
  const auto& scene = choice.scene;
  if (!scene) {
    return;
  }
  flecs::world world = e.world();
  const std::filesystem::path file = AssetFile(z13::building::primitives::BlueprintFile(*scene));
  const auto blocks = z13::ReadFile(file).and_then([&palette](const std::string& json) {
    return z13::building::primitives::ParseBlueprint(json, palette.palette);
  });
  // A blueprint that wouldn't build block by block isn't placed at all, rather than in part.
  const Status valid =
      blocks.and_then([&](const auto& parsed) { return ValidateBlueprint(parsed, palette.palette, tuning); });
  if (!valid) {
    log_error("station: no scene '{}': {} ({})", *scene, valid.error(), file.string());
    return;
  }
  // Observers defer commands; SpawnPlayer, right after this event, must see the spawn points.
  const z13::ImmediateScope immediate(world);
  for (size_t i = 0; i < blocks->size(); ++i) {
    CreateBlock(world, std::format("Station_{}", i), (*blocks)[i], palette.palette, tuning);
  }
}

void RegisterSystems(flecs::world world) {
  // Without the palette (CheckBlockPalette stops the launch first) this never fires.
  world
      .observer<z13::gameplay::PopulateSceneEvent, const StationSceneChoice, const BlockPalette, const BuildingTuning>(
          "StationSceneSystem::PopulateStationScene")
      .event<z13::gameplay::PopulateSceneEvent>()
      .with<StationMode>()
      .each(PopulateStationScene);
}

}  // namespace

void StationSceneSystem::Register(flecs::world& world) {
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
