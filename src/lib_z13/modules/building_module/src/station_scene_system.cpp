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

#include <algorithm>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <vector>

#include <flecs.h>

#include <lib_core/state/state_markers.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/station/conduit.h>
#include <z13/components/station/dock.h>
#include <z13/components/station/door.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/station/station_mode.h>
#include <primitives/blueprint.h>
#include <primitives/palette.h>
#include <primitives/station_assets.h>
#include <z13_settings/building_tuning.h>

#include "block_entities.h"
#include "blueprint_walls.h"
#include "build_validation.h"

namespace z13::building {

namespace {

using z13::building::primitives::BlockPalette;
using z13::station::StationMode;
using z13::station::StationSceneChoice;

// The blocks and wall panels of every grid of a blueprint must build, each in its own grid.
Status ValidateGrids(std::span<const z13::building::primitives::BlueprintGridPart> grids, const BlockPalette& palette,
                     const BuildingTuning& tuning) {
  for (const z13::building::primitives::BlueprintGridPart& part : grids) {
    auto panels = BlueprintPanels(part.walls, palette.palette, tuning, part.openings);
    if (!panels) {
      return std::unexpected(std::format("grid {}: {}", part.grid.id, panels.error()));
    }
    for (z13::station::Block& panel : *panels) {
      panel.grid = part.grid.id;
    }
    if (const Status valid = ValidateBlueprint(part.blocks, palette.palette, tuning, *panels); !valid) {
      return std::unexpected(std::format("grid {}: {}", part.grid.id, valid.error()));
    }
  }
  return {};
}

// The conduits' kinds are the conduit module's: without it a blueprint's runs are left out.
Status ResolveConduits(std::span<const z13::building::primitives::BlueprintConduit> runs,
                       const z13::conduit::ConduitKinds& kinds, std::vector<z13::conduit::Conduit>& resolved) {
  for (const z13::building::primitives::BlueprintConduit& run : runs) {
    const auto kind = std::ranges::find(kinds.kinds, run.kind, &z13::conduit::ConduitKind::name);
    if (kind == kinds.kinds.end()) {
      return std::unexpected(std::format("conduit kind '{}' is unknown", run.kind));
    }
    resolved.push_back(
        {.kind = kind->id, .grid = run.grid, .cell = run.cell, .axis = run.axis, .length = run.length});
  }
  return {};
}

void PopulateStationScene(
    flecs::entity e,
    z13::gameplay::PopulateSceneEvent,
    const StationSceneChoice& choice,
    const BlockPalette& palette,
    const BuildingTuning& tuning,
    const z13::Assets& assets) {
  const auto& scene = choice.scene;
  if (!scene) {
    return;
  }
  flecs::world world = e.world();
  const std::filesystem::path file = z13::building::primitives::BlueprintFile(*scene);
  const auto json = assets.source.ReadAsset(file);
  const auto blocks = json.and_then([&palette](const std::string& text) {
    return z13::building::primitives::ParseBlueprint(text, palette.palette);
  });
  const auto walls = json.and_then([](const std::string& text) {
    return z13::building::primitives::ParseBlueprintWalls(text);
  });
  const auto openings = json.and_then([](const std::string& text) {
    return z13::building::primitives::ParseBlueprintOpenings(text);
  });
  const auto grids = json.and_then([&palette](const std::string& text) {
    return z13::building::primitives::ParseBlueprintGrids(text, palette.palette);
  });
  const auto docks = json.and_then(z13::building::primitives::ParseBlueprintDocks);
  const auto conduit_runs = json.and_then(z13::building::primitives::ParseBlueprintConduits);
  const auto panels = walls.and_then([&](const auto& parsed) {
    return openings.and_then([&](const auto& cut) { return BlueprintPanels(parsed, palette.palette, tuning, cut); });
  });
  const auto* kinds = world.try_get<z13::conduit::ConduitKinds>();
  std::vector<z13::conduit::Conduit> conduits;
  // A blueprint that wouldn't build block by block isn't placed at all, rather than in part.
  const Status valid = blocks.and_then([&](const auto& parsed) -> Status {
    if (!panels) {
      return std::unexpected(panels.error());
    }
    if (!grids) {
      return std::unexpected(grids.error());
    }
    if (!docks) {
      return std::unexpected(docks.error());
    }
    if (!conduit_runs) {
      return std::unexpected(conduit_runs.error());
    }
    if (kinds != nullptr) {
      if (const Status resolved_ok = ResolveConduits(*conduit_runs, *kinds, conduits); !resolved_ok) {
        return resolved_ok;
      }
    }
    if (const Status main_valid = ValidateBlueprint(parsed, palette.palette, tuning, *panels); !main_valid) {
      return main_valid;
    }
    return ValidateGrids(*grids, palette, tuning);
  });
  if (!valid) {
    log_error("station: no scene '{}': {} ({})", *scene, valid.error(), file.string());
    return;
  }
  // Observers defer commands; SpawnPlayer, right after this event, must see the spawn points.
  const z13::ImmediateScope immediate(world);
  for (size_t i = 0; i < blocks->size(); ++i) {
    CreateBlock(world, std::format("Station_{}", i), (*blocks)[i], palette.palette, tuning);
  }
  z13::gameplay::SpawnNamer namer(StampNow(world, z13::gameplay::kSceneSpawner));
  SpawnBlueprintWalls(world, namer, *walls, *openings);
  for (const z13::building::primitives::BlueprintGridPart& part : *grids) {
    world.entity(std::format("Grid_{}", part.grid.id).c_str())
        .add<z13::flecs_tools::StateEntity>()
        .set(part.grid);
    for (size_t i = 0; i < part.blocks.size(); ++i) {
      CreateBlock(world, std::format("Station_{}_{}", part.grid.id, i), part.blocks[i], palette.palette, tuning);
    }
    SpawnBlueprintWalls(world, namer, part.walls, part.openings, part.grid.id);
  }
  for (size_t i = 0; i < docks->size(); ++i) {
    world.entity(std::format("Dock_{}", i).c_str()).add<z13::flecs_tools::StateEntity>().set((*docks)[i]);
  }
  for (size_t i = 0; i < conduits.size(); ++i) {
    world.entity(std::format("BlueprintConduit_{}", i).c_str()).add<z13::flecs_tools::StateEntity>().set(conduits[i]);
  }
  if (json.and_then(z13::building::primitives::ParseBlueprintDoorsOpen).value_or(false)) {
    world.set(z13::station::DoorsStartOpen {.open = true});
  }
}

void RegisterSystems(flecs::world world) {
  // Without the palette (CheckBlockPalette stops the launch first) this never fires.
  world
      .observer<z13::gameplay::PopulateSceneEvent, const StationSceneChoice, const BlockPalette, const BuildingTuning,
                const z13::Assets>(
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
