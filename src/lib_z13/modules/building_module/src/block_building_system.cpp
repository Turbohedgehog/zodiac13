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
#include <lib_core/utils/status.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/station.h>
#include <z13_grid/block_index.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>
#include <z13_settings/building_tuning.h>

#include "block_entities.h"
#include "brush_aim.h"
#include "build_validation.h"
#include "station_build_phase.h"

namespace z13::building {

namespace {

using z13::building::grid::BlockIndex;
using z13::building::primitives::BlockPalette;
using z13::building::primitives::CellBox;
using z13::building::primitives::OccupiedCells;
using z13::building::primitives::Palette;
using z13::station::Block;
using z13::station::BlockBrush;
using z13::station::BlockSpec;
using z13::station::BrushDrag;
using z13::station::BrushPreview;
using z13::station::BuildPermission;
using z13::station::kCellSize;
using z13::station::SpawnPoint;
using z13::station::StationMode;

using BlockQuery = flecs::query<const Block>;
using PlayerQuery = flecs::query<const z13::gameplay::PlayerCollider, const Eigen::Matrix4f>;
using SpawnBlockQuery = flecs::query<const Block>;

void RegisterPipeline(flecs::world world) {
  world.component<StationBuildPhase>().add(flecs::Phase).depends_on<z13::building::UpdateBuildingToolPhase>();
  world.component<z13::gameplay::PostUpdatePhase>().add(flecs::Phase).depends_on<StationBuildPhase>();
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<BlockIndex, BuildingTuning, BrushDrag, BuildPermission, BrushPreview>(world);
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

std::optional<Aim> FindAim(flecs::entity player, const BlockIndex& index) {
  std::optional<Aim> aim;
  player.children([&aim, player, &index](flecs::entity child) {
    if (aim || !child.has<z13::building::Brush>() || !child.has<Eigen::Matrix4f>() ||
        !player.has<Eigen::Matrix4f>()) {
      return;
    }
    const Eigen::Vector3f eye = z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) / kCellSize;
    const Eigen::Vector3f reach = z13::math::ExtractTranslation<float>(child.get<Eigen::Matrix4f>()) / kCellSize;
    aim = AimAt(index, eye, reach);
  });
  return aim;
}

std::optional<BrushDrag> DragOf(flecs::entity player) {
  return player.has<BrushDrag>() ? std::optional(player.get<BrushDrag>()) : std::nullopt;
}

// What a build would place with the brush aimed at `aim`: the block dragged out from
// `drag`, or without one (or before it moved) the brush's block where it can rest (see BlockAt).
Block BrushBlock(const std::optional<BrushDrag>& drag, const BlockSpec& spec, const Aim& aim,
                 const Palette& palette, const BlockIndex& index) {
  if (!drag || drag->anchor_cell == aim.Cell()) {
    return BlockAt(aim, spec, index);
  }
  const auto primitive = palette.Find(spec.type_id);
  return z13::building::primitives::DraggedBlock(
      drag->anchor_cell, aim.Cell(), spec, primitive ? primitive->get().min_size : spec.size,
      primitive ? primitive->get().max_size : spec.size, drag->anchor_normal);
}

Status CheckPermission(flecs::entity player) {
  if (!player.has<BuildPermission>()) {
    return std::unexpected(std::string {"no permission to build"});
  }
  return {};
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

// Named like the ship scene's blocks; see SpawnCube in BuildingSystem for the lookup loop.
std::string NextBlockName(flecs::world world, z13::gameplay::IdCounters& counters) {
  std::string name;
  do {
    name = std::format("Block_{}", ++counters.last_block_id);
  } while (world.lookup(name.c_str()));
  return name;
}

void StartBrushDrag(flecs::entity player, RequestBrushDrag, const BlockIndex& index) {
  player.remove<RequestBrushDrag>();
  if (const auto aim = FindAim(player, index)) {
    player.set(BrushDrag {.anchor_cell = aim->Cell(), .anchor_normal = aim->normal});
  }
}

// A dragged block's size becomes the brush's, so the next click repeats it.
void ProcessBuildRequest(
    flecs::entity player, BlockBrush& brush, BlockIndex& index, z13::gameplay::IdCounters& counters,
    const BlockPalette& palette, const BuildingTuning& tuning, const PlayerQuery& players,
    std::vector<CellBox>& spawn_clearances) {
  player.remove<z13::building::RequestBuildBlock>();
  const auto aim = FindAim(player, index);
  const auto drag = DragOf(player);
  player.remove<BrushDrag>();
  if (!aim) {
    return;
  }

  const Block block = BrushBlock(drag, brush.spec, *aim, palette.palette, index);
  const auto valid = CheckPermission(player).and_then([&] {
    return ValidateBuild(block, palette.palette, index, PlayerSpheres(players), spawn_clearances, tuning);
  });
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
  if (drag) {
    brush.spec = block.spec;
  }
}

// A run() rather than each(), like ProcessDestroyRequests: a spawn point built by one
// request keeps its clearance free for the requests after it in the same tick.
void ProcessBuildRequests(flecs::iter& it, const PlayerQuery& players, const SpawnBlockQuery& spawn_blocks) {
  std::optional<std::vector<CellBox>> spawn_clearances;
  while (it.next()) {
    auto brushes = it.field<BlockBrush>(2);
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
      if (const auto permitted = CheckPermission(player); !permitted) {
        log_debug("station: destroy refused: {}", permitted.error());
        continue;
      }

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

void UpdateBrushPreview(
    flecs::entity brush, const BlockIndex& index, const BlockPalette& palette, const BuildingTuning& tuning,
    const PlayerQuery& players, const SpawnBlockQuery& spawn_blocks) {
  const flecs::entity owner = brush.parent();
  if (!owner || !owner.has<BlockBrush>()) {
    return;
  }
  const auto aim = FindAim(owner, index);
  if (!aim) {
    return;
  }
  const Block block = BrushBlock(DragOf(owner), owner.get<BlockBrush>().spec, *aim, palette.palette, index);
  const bool valid = CheckPermission(owner) &&
                     ValidateBuild(block, palette.palette, index, PlayerSpheres(players),
                                   SpawnClearances(spawn_blocks, tuning), tuning);
  brush.set(BrushPreview {.block = block, .valid = valid});
}

void InstallIndex(flecs::world world) {
  world.set(BlockIndex {});
}

void RegisterSystems(flecs::world world) {
  InstallIndex(world);

  const BlockQuery blocks = world.query_builder<const Block>("BlockBuildingSystem::BlockQuery").detect_changes().build();
  world.system<BlockIndex, const BuildingTuning>("BlockBuildingSystem::SyncBlockIndex")
      .kind<StationBuildPhase>()
      .with<StationMode>()
      .read<Block>()
      .each([blocks](flecs::iter&, size_t, BlockIndex& index, const BuildingTuning& tuning) {
        SyncBlockIndex(blocks, index, tuning);
      });

  world.system<RequestBrushDrag, const BlockIndex>("BlockBuildingSystem::StartBrushDrag")
      .kind<StationBuildPhase>()
      .with<StationMode>()
      .write<BrushDrag>()
      .each(StartBrushDrag);

  const PlayerQuery players =
      world.query_builder<const z13::gameplay::PlayerCollider, const Eigen::Matrix4f>("BlockBuildingSystem::Players")
          .with<z13::gameplay::Player>()
          .build();
  const SpawnBlockQuery spawn_blocks =
      world.query_builder<const Block>("BlockBuildingSystem::SpawnBlocks").with<SpawnPoint>().build();

  // Same-tick requests compete, so they go in player id order (see CompareByPlayerId).
  world.system<z13::building::RequestBuildBlock, const z13::gameplay::Player, BlockBrush, BlockIndex,
               z13::gameplay::IdCounters, const BlockPalette, const BuildingTuning>(
      "BlockBuildingSystem::ProcessBuildRequests")
      .kind<StationBuildPhase>()
      .term_at(3).inout()
      .term_at(4).inout()
      .with<StationMode>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .read<BuildPermission>()
      .write<BrushDrag>()
      .write<Block>()
      .write<SpawnPoint>()
      .run([players, spawn_blocks](flecs::iter& it) { ProcessBuildRequests(it, players, spawn_blocks); });

  world.system<z13::building::RequestDestroyBlock, const z13::gameplay::Player, const Eigen::Matrix4f, BlockIndex,
               const BlockPalette, const BuildingTuning>("BlockBuildingSystem::ProcessDestroyRequests")
      .kind<StationBuildPhase>()
      .term_at(3).inout()
      .with<StationMode>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .read<BuildPermission>()
      .read<SpawnPoint>()
      .write<Block>()
      .run(ProcessDestroyRequests);

  // Last, so the preview shows this frame's blocks; the render draws it in OnStore.
  world.system<const Eigen::Matrix4f, const BlockIndex, const BlockPalette, const BuildingTuning>(
           "BlockBuildingSystem::UpdateBrushPreview")
      .kind<StationBuildPhase>()
      .with<z13::building::Brush>()
      .with<StationMode>()
      .read<BlockBrush>()
      .read<BrushDrag>()
      .read<BuildPermission>()
      .write<BrushPreview>()
      .each([players, spawn_blocks](flecs::entity brush, const Eigen::Matrix4f&, const BlockIndex& index,
                                    const BlockPalette& palette, const BuildingTuning& tuning) {
        UpdateBrushPreview(brush, index, palette, tuning, players, spawn_blocks);
      });
}

}  // namespace

void BlockBuildingSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
