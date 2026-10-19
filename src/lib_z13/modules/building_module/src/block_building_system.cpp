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

#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/status.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <z13/components/block_change_reader.h>
#include <z13/components/block_changes.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/gameplay_phases.h>
#include <z13/components/player.h>
#include <z13/components/player_parts.h>
#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <z13/components/inventory.h>
#include <z13/components/spawn_point.h>
#include <z13/components/station_mode.h>
#include <grid/block_index.h>
#include <primitives/occupancy.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13_settings/building_tuning.h>

#include "block_cutting.h"
#include "block_entities.h"
#include "build_run.h"
#include "brush_aim.h"
#include "build_validation.h"
#include "held_tool.h"
#include "station_build_phase.h"
#include "wall_brush.h"
#include "wall_entities.h"
#include "wall_merge.h"

namespace z13::building {

namespace {

using z13::building::grid::BlockIndex;
using z13::building::primitives::BlockPalette;
using z13::building::primitives::CellBox;
using z13::building::primitives::OccupiedCells;
using z13::building::primitives::Palette;
using z13::building::primitives::Shape;
using z13::building::primitives::ShapeOfBlock;
using z13::station::Block;
using z13::station::BlockBrush;
using z13::station::BlockSpec;
using z13::station::BrushDrag;
using z13::station::BrushPreview;
using BuildKind = BrushPreview::Kind;
using z13::station::BuildPermission;
using z13::station::kCellSize;
using z13::station::SpawnPoint;
using z13::station::StationMode;
using z13::station::WallDraft;
using z13::station::WallMergeDraft;
using z13::station::WallPanelOf;
using z13::station::WallNode;
using z13::station::WallSegment;

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

void RebuildIndex(const BlockQuery& blocks, BlockIndex& index, const BuildingTuning& tuning, const Palette& palette) {
  Z13_PROFILE_ZONE("RebuildIndex");
  index = BlockIndex(tuning.index_chunk_cells);
  blocks.each([&index, &palette](flecs::entity e, const Block& block) {
    index.Insert(e.id(), ShapeOfBlock(block, palette));
  });
}

// Build and destroy requests also update the index themselves, for later requests in the tick.
void SyncBlockIndex(z13::station::BlockChangeReader& reader, uint64_t restores, uint64_t tick, BlockIndex& index,
                    const BuildingTuning& tuning, const Palette& palette) {
  const z13::station::BlockChangeBatch batch = reader.Read(restores, tick, index.Size());
  if (batch.resync || index.ChunkCells() != tuning.index_chunk_cells) {
    RebuildIndex(reader.Blocks(), index, tuning, palette);
    return;
  }
  for (const z13::station::BlockChange& change : batch.changes) {
    if (change.kind == z13::station::BlockChangeKind::kRemoved) {
      index.Erase(change.entity);
    } else {
      index.Insert(change.entity, ShapeOfBlock(change.block, palette));
    }
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
// When `kind` cuts the block goes into the aimed face, not against it, over what it cuts.
Block BrushBlock(const std::optional<BrushDrag>& drag, const BlockSpec& spec, const Aim& aimed, const BuildView& view,
                 BuildKind kind) {
  const bool cutting = kind != BuildKind::kBuild;
  const Aim aim = cutting ? aimed.Flipped() : aimed;
  const Eigen::Vector3i anchor_cell = drag ? (cutting ? drag->anchor_cell - drag->anchor_normal : drag->anchor_cell)
                                           : Eigen::Vector3i::Zero();
  if (!drag || anchor_cell == aim.Cell()) {
    return BlockAt(aim, spec, view.index, cutting);
  }
  const auto primitive = view.palette.get().Find(spec.type_id);
  return z13::building::primitives::DraggedBlock(
      anchor_cell, aim.Cell(), spec, primitive ? primitive->get().min_size : spec.size,
      primitive ? primitive->get().max_size : spec.size, cutting ? -drag->anchor_normal : drag->anchor_normal,
      primitive ? primitive->get().shape : Shape {});
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

void StartBrushDrag(flecs::entity player, RequestBrushDrag, const BlockIndex& index) {
  player.remove<RequestBrushDrag>();
  if (const auto aim = FindAim(player, index)) {
    player.set(BrushDrag {.anchor_cell = aim->Cell(), .anchor_normal = aim->normal});
  }
}

BuildKind KindOf(flecs::entity player) {
  if (player.has<z13::building::CutModifier>()) {
    return BuildKind::kCut;
  }
  return player.has<z13::building::CutInModifier>() ? BuildKind::kCutIn : BuildKind::kBuild;
}

bool IsSpawnPoint(const Block& block, const Palette& palette) {
  const auto primitive = palette.Find(block.spec.type_id);
  return primitive && primitive->get().Has(z13::building::primitives::PrimitiveFlags::Spawn);
}

std::optional<WallDraft> DraftOf(flecs::entity player) {
  return player.has<WallDraft>() ? std::optional(player.get<WallDraft>()) : std::nullopt;
}

// A click of the Wall mode: it sets the draft's next point, and the third makes the wall
// from its nodes (existing ones where it snapped to them).
void ProcessWallClick(flecs::entity player, flecs::entity tool, const Aim& aim, BuildRun& run) {
  flecs::world world = player.world();
  const std::optional<WallDraft> draft = DraftOf(player);
  const Eigen::Vector3i point = WallClickPoint(aim, WallAnchors(world), run.tuning.get().wall_snap_cells);
  const auto click = CheckPermission(player).and_then([&] {
    return ClickWall(draft, point, tool.get<BuildTool>().wall_thickness);
  });
  if (!click) {
    log_debug("station: wall click refused: {}", click.error());
    return;
  }
  if (click->draft) {
    player.set(*click->draft);
    return;
  }
  const auto faced = FacingEye(*click->wall, aim.eye, run.palette);
  const auto valid = faced.and_then([&](const FacedWall& wall) { return ValidateBuild(wall.block, run.View()); });
  if (!valid) {
    log_debug("station: wall refused: {}", valid.error());
    return;
  }
  const uint32_t node_a = WallNodeAt(world, run.counters, faced->wall.node_a);
  const uint32_t node_b = WallNodeAt(world, run.counters, faced->wall.node_b);
  SpawnWallSegment(world, run.counters,
                   WallSegment {.node_a = node_a, .node_b = node_b, .stick_axis = faced->wall.stick_axis,
                                .stick_length = faced->wall.stick_length, .thickness = faced->wall.thickness});
  player.remove<WallDraft>();
}

std::optional<uint32_t> MergeDraftNode(flecs::entity player) {
  return player.has<WallMergeDraft>() ? std::optional(player.get<WallMergeDraft>().node_id) : std::nullopt;
}

// A click of the Merge mode: the first picks a wall node, the second merges it with another.
void ProcessMergeClick(flecs::entity player, flecs::entity tool, const Aim& aim, BuildRun& run) {
  flecs::world world = player.world();
  const std::optional<uint32_t> clicked = NearestWallNode(world, aim, run.tuning.get().wall_snap_cells);
  const std::optional<uint32_t> first = MergeDraftNode(player);
  const auto plan = CheckPermission(player).and_then([&]() -> std::expected<std::optional<NodeMerge>, std::string> {
    if (!clicked) {
      return std::unexpected(std::string {"no wall node there"});
    }
    if (!first) {
      return std::nullopt;
    }
    return PlanNodeMerge(world, *first, *clicked, tool.get<BuildTool>().merge_at, run.palette).transform(
        [](const NodeMerge& merge) { return std::optional(merge); });
  });
  if (!plan) {
    log_debug("station: merge click refused: {}", plan.error());
    return;
  }
  if (!*plan) {
    player.set(WallMergeDraft {.node_id = *clicked});
    return;
  }
  ApplyNodeMerge(world, run.counters, **plan);
  player.remove<WallMergeDraft>();
}

// A dragged block's size becomes the brush's, so the next click repeats it. With CutModifier
// the block's cells are cut out of the station instead; with CutInModifier they are cut
// out first and the block takes their place.
void ProcessBuildRequest(flecs::entity player, BuildRun& run) {
  player.remove<z13::building::RequestBuildBlock>();
  const std::optional<flecs::entity> tool = z13::building::HeldBuildTool(player);
  if (!tool || !tool->has<BlockBrush>()) {
    player.remove<BrushDrag>();
    return;
  }
  const auto aim = FindAim(player, run.index);
  if (tool->get<BuildTool>().mode == ToolMode::kWall) {
    if (aim) {
      ProcessWallClick(player, *tool, *aim, run);
    }
    return;
  }
  if (tool->get<BuildTool>().mode == ToolMode::kMerge) {
    if (aim) {
      ProcessMergeClick(player, *tool, *aim, run);
    }
    return;
  }
  BlockBrush& brush = tool->get_mut<BlockBrush>();
  const auto drag = DragOf(player);
  player.remove<BrushDrag>();
  if (!aim) {
    return;
  }

  const BuildKind kind = KindOf(player);
  const Block block = BrushBlock(drag, brush.spec, *aim, run.View(), kind);
  const bool spawns = IsSpawnPoint(block, run.palette);
  const auto valid = CheckPermission(player).and_then([&] {
    return kind == BuildKind::kCut ? Status {} : ValidateBuild(block, run.View(), kind);
  });
  if (!valid) {
    log_debug("station: build refused: {}", valid.error());
    return;
  }

  flecs::world world = player.world();
  if (kind != BuildKind::kBuild) {
    const CutRequest request {
        .cells = OccupiedCells(block), .spawn_points = run.spawn_points,
        .spawn_points_added = kind == BuildKind::kCutIn && spawns ? 1 : 0};
    const auto plan = PlanCut(world, run.View(), run.made, request);
    if (!plan) {
      log_debug("station: cut refused: {}", plan.error());
      return;
    }
    ApplyCut(world, *plan, run);
    run.spawn_points -= plan->spawn_points_removed;
  }
  if (kind != BuildKind::kCut) {
    const flecs::entity created =
        CreateBlock(world, NextBlockName(world, run.counters), block, run.palette, run.tuning);
    run.index.get().Insert(created.id(), ShapeOfBlock(block, run.palette));
    run.made.emplace(created.id(), block);
    if (spawns) {
      ++run.spawn_points;
      run.spawn_clearances.push_back(SpawnClearance(block, run.tuning));
    }
  }
  if (drag) {
    brush.spec = block.spec;
  }
}

// A run() rather than each(), like ProcessDestroyRequests: a spawn point built by one
// request keeps its clearance free for the requests after it in the same tick.
void ProcessBuildRequests(flecs::iter& it, const PlayerQuery& players, const SpawnBlockQuery& spawn_blocks) {
  std::optional<BuildRun> run;
  while (it.next()) {
    if (!run) {
      const auto& tuning = it.field<const BuildingTuning>(5)[0];
      run = BuildRun {
          .index = it.field<BlockIndex>(2)[0],
          .counters = it.field<z13::gameplay::IdCounters>(3)[0],
          .palette = it.field<const BlockPalette>(4)[0].palette,
          .tuning = tuning,
          .players = PlayerSpheres(players),
          .spawn_clearances = SpawnClearances(spawn_blocks, tuning),
          .spawn_points = it.world().count<SpawnPoint>()};
    }
    for (const size_t i : it) {
      ProcessBuildRequest(it.entity(i), *run);
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
      if (target.has<WallPanelOf>()) {
        // The segment owns its panel: removing it takes the panel along.
        DestroyWallSegment(it.world(), target.get<WallPanelOf>().segment_id);
        continue;
      }
      if (target.has<SpawnPoint>()) {
        --spawn_points;
      }
      index.Erase(*hit);
      z13::station::DestroyBlock(target, block);
    }
  }
}

BrushPreview WallBrushPreview(flecs::entity owner, flecs::entity tool, const Aim& aim, const BuildView& view) {
  const std::optional<WallDraft> draft = DraftOf(owner);
  const Eigen::Vector3i point = WallClickPoint(aim, WallAnchors(owner.world()), view.tuning.get().wall_snap_cells);
  const WallPreview preview =
      PreviewWall(draft, point, tool.get<BuildTool>().wall_thickness, aim.eye, view.palette);
  const bool panel = draft && draft->stage == WallDraft::Stage::kEnd;
  const bool valid = preview.makeable && CheckPermission(owner).has_value() &&
                     (!panel || ValidateBuild(preview.block, view).has_value());
  return {.block = preview.block, .valid = valid, .kind = BuildKind::kBuild};
}

BrushPreview MergeBrushPreview(flecs::entity owner, flecs::entity tool, const Aim& aim, const BuildView& view) {
  flecs::world world = owner.world();
  const std::optional<uint32_t> hovered = NearestWallNode(world, aim, view.tuning.get().wall_snap_cells);
  const std::optional<uint32_t> first = MergeDraftNode(owner);
  const std::optional<Eigen::Vector3i> cell = hovered ? WallNodeCell(world, *hovered) : std::nullopt;
  bool valid = hovered.has_value() && CheckPermission(owner).has_value();
  Eigen::Vector3i shown = cell.value_or(aim.point.array().round().cast<int>().matrix());
  if (valid && first) {
    const auto plan = PlanNodeMerge(world, *first, *hovered, tool.get<BuildTool>().merge_at, view.palette);
    valid = plan.has_value();
    shown = plan ? plan->cell : shown;
  }
  return {.block = CellMarker(shown), .valid = valid, .kind = BuildKind::kBuild};
}

void UpdateBrushPreview(flecs::entity brush, const BuildView& view) {
  const flecs::entity owner = brush.parent();
  const std::optional<flecs::entity> tool = owner ? z13::building::HeldBuildTool(owner) : std::nullopt;
  if (!tool || !tool->has<BlockBrush>()) {
    return;
  }
  const auto aim = FindAim(owner, view.index);
  if (!aim) {
    return;
  }
  if (tool->get<BuildTool>().mode == ToolMode::kWall) {
    brush.set(WallBrushPreview(owner, *tool, *aim, view));
    return;
  }
  if (tool->get<BuildTool>().mode == ToolMode::kMerge) {
    brush.set(MergeBrushPreview(owner, *tool, *aim, view));
    return;
  }
  const BuildKind kind = KindOf(owner);
  const Block block = BrushBlock(DragOf(owner), tool->get<BlockBrush>().spec, *aim, view, kind);
  const auto fits = [&] { return kind == BuildKind::kCut || ValidateBuild(block, view, kind).has_value(); };
  const auto cuttable = [&] {
    const CutRequest request {
        .cells = OccupiedCells(block), .spawn_points = owner.world().count<SpawnPoint>(),
        .spawn_points_added = kind == BuildKind::kCutIn && IsSpawnPoint(block, view.palette) ? 1 : 0};
    return kind == BuildKind::kBuild || PlanCut(owner.world(), view, {}, request).has_value();
  };
  const bool valid = CheckPermission(owner).has_value() && fits() && cuttable();
  if (kind == BuildKind::kCut) {
    const CellBox cells = OccupiedCells(block);
    const Block box {.spec = {.type_id = z13::station::kCubePrimitiveId, .size = cells.extent}, .cell = cells.min};
    brush.set(BrushPreview {.block = box, .valid = valid, .kind = kind});
    return;
  }
  brush.set(BrushPreview {.block = block, .valid = valid, .kind = kind});
}

void InstallIndex(flecs::world world) {
  world.set(BlockIndex {});
}

void RegisterSystems(flecs::world world) {
  InstallIndex(world);

  const auto reader = std::make_shared<z13::station::BlockChangeReader>(world);
  world.system<BlockIndex, const z13::flecs_tools::WorldRestores, const z13::flecs_tools::SimulationClock,
               const BuildingTuning, const BlockPalette>("BlockBuildingSystem::SyncBlockIndex")
      .kind<StationBuildPhase>()
      .with<StationMode>()
      .read<Block>()
      .read<z13::station::BlockChange>()
      .each([reader](flecs::iter&, size_t, BlockIndex& index, const z13::flecs_tools::WorldRestores& restores,
                     const z13::flecs_tools::SimulationClock& clock, const BuildingTuning& tuning,
                     const BlockPalette& palette) {
        SyncBlockIndex(*reader, restores.count, clock.tick, index, tuning, palette.palette);
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
  world.system<z13::building::RequestBuildBlock, const z13::gameplay::Player, BlockIndex,
               z13::gameplay::IdCounters, const BlockPalette, const BuildingTuning>(
      "BlockBuildingSystem::ProcessBuildRequests")
      .kind<StationBuildPhase>()
      .term_at(2).inout()
      .term_at(3).inout()
      .with<StationMode>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .read<z13::inventory::ActiveItem>()
      .write<BlockBrush>()
      .read<BuildPermission>()
      .read<z13::building::CutModifier>()
      .read<z13::building::CutInModifier>()
      .write<BrushDrag>()
      .write<Block>()
      .write<z13::station::BlockChange>()
      .write<SpawnPoint>()
      .write<WallDraft>()
      .write<WallMergeDraft>()
      .write<WallNode>()
      .write<WallSegment>()
      .run([players, spawn_blocks](flecs::iter& it) { ProcessBuildRequests(it, players, spawn_blocks); });

  world.system<z13::building::RequestDestroyBlock, const z13::gameplay::Player, const Eigen::Matrix4f, BlockIndex,
               const BlockPalette, const BuildingTuning>("BlockBuildingSystem::ProcessDestroyRequests")
      .kind<StationBuildPhase>()
      .term_at(3).inout()
      .with<StationMode>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .read<BuildPermission>()
      .read<SpawnPoint>()
      .read<WallPanelOf>()
      .write<Block>()
      .write<z13::station::BlockChange>()
      .write<z13::station::WallSegment>()
      .write<z13::station::WallNode>()
      .run(ProcessDestroyRequests);

  // Last, so the preview shows this frame's blocks; the render draws it in OnStore.
  world.system<const Eigen::Matrix4f, const BlockIndex, const BlockPalette, const BuildingTuning>(
           "BlockBuildingSystem::UpdateBrushPreview")
      .kind<StationBuildPhase>()
      .with<z13::building::Brush>()
      .with<StationMode>()
      .read<z13::inventory::ActiveItem>()
      .read<BlockBrush>()
      .read<Block>()
      .read<z13::building::CutModifier>()
      .read<z13::building::CutInModifier>()
      .read<BrushDrag>()
      .read<BuildPermission>()
      .read<WallDraft>()
      .read<WallMergeDraft>()
      .read<WallNode>()
      .read<WallSegment>()
      .write<BrushPreview>()
      .each([players, spawn_blocks](flecs::entity brush, const Eigen::Matrix4f&, const BlockIndex& index,
                                    const BlockPalette& palette, const BuildingTuning& tuning) {
        const std::vector<PlayerSphere> spheres = PlayerSpheres(players);
        const std::vector<CellBox> clearances = SpawnClearances(spawn_blocks, tuning);
        UpdateBrushPreview(brush, {.palette = palette.palette, .index = index, .tuning = tuning,
                                   .players = spheres, .spawn_clearances = clearances});
      });
}

}  // namespace

void BlockBuildingSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
