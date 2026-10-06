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

#include "block_building_system.h"

#include <format>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/station.h>
#include <z13_grid/block_index.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>
#include <z13_settings/building_tuning.h>

#include "block_entities.h"
#include "build_validation.h"

namespace z13::building {

// Outside the anonymous namespace: its path breaks phase-order ties (see phase_order.h).
struct StationBuildPhase {};

namespace {

using z13::building::grid::BlockIndex;
using z13::station::Block;
using z13::station::BlockBrush;
using z13::station::kCellSize;
using z13::station::SpawnPoint;
using z13::station::StationMode;
using z13::building::primitives::BlockPalette;
using z13::building::primitives::CellBox;
using z13::building::primitives::OccupiedCells;
using z13::building::primitives::PlaceCentredOn;

// Until the palette UI (f/build-palette-ui) lets players pick: a 2 m square wall panel.
constexpr uint32_t kDefaultBrushType = 2;
constexpr int kDefaultBrushWidthCells = 8;

using BlockQuery = flecs::query<const Block>;
using PlayerQuery = flecs::query<const z13::gameplay::PlayerCollider, const Eigen::Matrix4f>;
using SpawnBlockQuery = flecs::query<const Block>;

void RegisterPipeline(flecs::world world) {
  world.component<StationBuildPhase>().add(flecs::Phase).depends_on<z13::building::UpdateBuildingToolPhase>();
  world.component<z13::gameplay::PostUpdatePhase>().add(flecs::Phase).depends_on<StationBuildPhase>();
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<BlockIndex, BuildingTuning>(world);
}

void EnsureBlockBrush(flecs::entity player, const z13::gameplay::Player&) {
  player.set(BlockBrush {
      .spec = {.type_id = kDefaultBrushType, .size = {kDefaultBrushWidthCells, 1, kDefaultBrushWidthCells}},
  });
}

void RebuildIndex(const BlockQuery& blocks, BlockIndex& index, const BuildingTuning& tuning) {
  index = BlockIndex(tuning.index_chunk_cells);
  blocks.each([&index](flecs::entity e, const Block& block) { index.Insert(e.id(), OccupiedCells(block)); });
}

// Checked before count(): iterating the query resets its changed state.
void SyncBlockIndex(const BlockQuery& blocks, BlockIndex& index, const BuildingTuning& tuning) {
  if (blocks.changed() || static_cast<size_t>(blocks.count()) != index.Size() ||
      index.ChunkCells() != tuning.index_chunk_cells) {
    RebuildIndex(blocks, index, tuning);
  }
}

std::optional<Eigen::Vector3f> FindBrushPosition(flecs::entity player) {
  std::optional<Eigen::Vector3f> position;
  player.children([&position](flecs::entity child) {
    if (!position && child.has<z13::building::Brush>() && child.has<Eigen::Matrix4f>()) {
      position = z13::math::ExtractTranslation<float>(child.get<Eigen::Matrix4f>());
    }
  });
  return position;
}

std::vector<PlayerSphere> PlayerSpheres(const PlayerQuery& players) {
  std::vector<PlayerSphere> spheres;
  players.each([&spheres](const z13::gameplay::PlayerCollider& collider, const Eigen::Matrix4f& transform) {
    spheres.push_back({.center = z13::math::ExtractTranslation<float>(transform), .radius = collider.radius});
  });
  return spheres;
}

std::vector<CellBox> SpawnClearances(const SpawnBlockQuery& spawn_blocks, const BuildingTuning& tuning) {
  std::vector<CellBox> clearances;
  spawn_blocks.each([&clearances, &tuning](const Block& marker) {
    clearances.push_back(SpawnClearance(marker, tuning));
  });
  return clearances;
}

// Named like the ship scene's blocks; see SpawnCube in z13_module for the lookup loop.
std::string NextBlockName(flecs::world world, z13::gameplay::IdCounters& counters) {
  std::string name;
  do {
    name = std::format("Block_{}", ++counters.last_block_id);
  } while (world.lookup(name.c_str()));
  return name;
}

void ProcessBuildRequest(
    flecs::entity player, const BlockBrush& brush, BlockIndex& index, z13::gameplay::IdCounters& counters,
    const BlockPalette& palette, const BuildingTuning& tuning, const PlayerQuery& players,
    std::vector<CellBox>& spawn_clearances) {
  player.remove<z13::building::RequestBuildBlock>();
  const auto brush_position = FindBrushPosition(player);
  if (!brush_position) {
    return;
  }

  const Block block = PlaceCentredOn(*brush_position / kCellSize, brush.spec);
  const auto valid = ValidateBuild(block, palette.palette, index, PlayerSpheres(players), spawn_clearances, tuning);
  if (!valid) {
    log_debug("station: build refused: {}", valid.error());
    return;
  }

  flecs::world world = player.world();
  const flecs::entity created = CreateBlock(world, NextBlockName(world, counters), block, palette.palette, tuning);
  // Now, not at the next sync: a later request this same tick must see these cells taken.
  index.Insert(created.id(), OccupiedCells(block));
  const auto primitive = palette.palette.Find(block.spec.type_id);
  if (primitive && primitive->get().Has(z13::building::primitives::PrimitiveFlags::Spawn)) {
    spawn_clearances.push_back(SpawnClearance(block, tuning));
  }
}

// A run() rather than each(), like ProcessDestroyRequests: a spawn point built by one
// request keeps its clearance free for the requests after it in the same tick.
void ProcessBuildRequests(flecs::iter& it, const PlayerQuery& players, const SpawnBlockQuery& spawn_blocks) {
  std::optional<std::vector<CellBox>> spawn_clearances;
  while (it.next()) {
    const auto brushes = it.field<const BlockBrush>(2);
    auto& index = it.field<BlockIndex>(3)[0];
    auto& counters = it.field<z13::gameplay::IdCounters>(4)[0];
    const auto& palette = it.field<const BlockPalette>(5)[0];
    const auto& tuning = it.field<const BuildingTuning>(6)[0];
    if (!spawn_clearances) {
      spawn_clearances = SpawnClearances(spawn_blocks, tuning);
    }
    for (const size_t i : it) {
      ProcessBuildRequest(it.entity(i), brushes[i], index, counters, palette, tuning, players, *spawn_clearances);
    }
  }
}

// A run() rather than each(): the spawn point count carries across requests, so two
// players can't each remove one of the last two in the same tick.
void ProcessDestroyRequests(flecs::iter& it) {
  int spawn_points = it.world().count<SpawnPoint>();
  while (it.next()) {
    const auto transforms = it.field<const Eigen::Matrix4f>(2);
    auto& index = it.field<BlockIndex>(3)[0];
    const auto& palette = it.field<const BlockPalette>(4)[0];
    const auto& tuning = it.field<const BuildingTuning>(5)[0];
    for (const size_t i : it) {
      flecs::entity player = it.entity(i);
      player.remove<z13::building::RequestDestroyBlock>();

      const Eigen::Vector3f origin = z13::math::ExtractTranslation<float>(transforms[i]);
      const Eigen::Vector3f forward = transforms[i].block<3, 3>(0, 0).col(0);
      const auto hit =
          index.Raycast(origin / kCellSize, (origin + forward * tuning.destroy_reach_distance) / kCellSize);
      if (!hit) {
        continue;
      }
      const flecs::entity target = it.world().entity(*hit);
      const Block& block = target.get<Block>();
      if (const auto valid = ValidateDestroy(block, palette.palette, spawn_points); !valid) {
        log_debug("station: destroy refused: {}", valid.error());
        continue;
      }
      if (target.has<SpawnPoint>()) {
        --spawn_points;
      }
      index.Erase(*hit);
      target.destruct();
    }
  }
}

void InstallIndex(flecs::world world) {
  world.set(BlockIndex {});
}

void RegisterSystems(flecs::world world) {
  InstallIndex(world);

  world.system<const z13::gameplay::Player>("BlockBuildingSystem::EnsureBlockBrush")
      .kind<StationBuildPhase>()
      .with<StationMode>()
      .without<BlockBrush>()
      .write<BlockBrush>()
      .each(EnsureBlockBrush);

  const BlockQuery blocks = world.query_builder<const Block>("BlockBuildingSystem::BlockQuery").detect_changes().build();
  world.system<BlockIndex, const BuildingTuning>("BlockBuildingSystem::SyncBlockIndex")
      .kind<StationBuildPhase>()
      .with<StationMode>()
      .read<Block>()
      .each([blocks](flecs::iter&, size_t, BlockIndex& index, const BuildingTuning& tuning) {
        SyncBlockIndex(blocks, index, tuning);
      });

  const PlayerQuery players =
      world.query_builder<const z13::gameplay::PlayerCollider, const Eigen::Matrix4f>("BlockBuildingSystem::Players")
          .with<z13::gameplay::Player>()
          .build();
  const SpawnBlockQuery spawn_blocks =
      world.query_builder<const Block>("BlockBuildingSystem::SpawnBlocks").with<SpawnPoint>().build();

  // Same-tick requests compete, so they go in player id order (see CompareByPlayerId).
  world.system<z13::building::RequestBuildBlock, const z13::gameplay::Player, const BlockBrush, BlockIndex,
               z13::gameplay::IdCounters, const BlockPalette, const BuildingTuning>(
      "BlockBuildingSystem::ProcessBuildRequests")
      .kind<StationBuildPhase>()
      .term_at(3).inout()
      .term_at(4).inout()
      .with<StationMode>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .write<Block>()
      .write<SpawnPoint>()
      .run([players, spawn_blocks](flecs::iter& it) { ProcessBuildRequests(it, players, spawn_blocks); });

  world.system<z13::building::RequestDestroyBlock, const z13::gameplay::Player, const Eigen::Matrix4f, BlockIndex,
               const BlockPalette, const BuildingTuning>("BlockBuildingSystem::ProcessDestroyRequests")
      .kind<StationBuildPhase>()
      .term_at(3).inout()
      .with<StationMode>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .read<SpawnPoint>()
      .write<Block>()
      .run(ProcessDestroyRequests);
}

}  // namespace

void BlockBuildingSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
