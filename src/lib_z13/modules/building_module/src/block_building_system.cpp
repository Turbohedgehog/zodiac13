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
#include <unordered_map>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <primitives_generated.h>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/status.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <lib_core/state/state_markers.h>

#include <z13/components/station/block_change_reader.h>
#include <z13/components/station/block_changes.h>
#include <z13/components/station/building.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/gameplay/gameplay_phases.h>
#include <z13/components/station/grid.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/gameplay/player_parts.h>
#include <z13/components/station/block.h>
#include <z13/components/station/block_brush.h>
#include <z13/components/gameplay/inventory.h>
#include <z13/components/station/spawn_point.h>
#include <z13/components/station/station_mode.h>
#include <grid/block_index.h>
#include <primitives/occupancy.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13_settings/building_tuning.h>

#include "block_cutting.h"
#include "build_grids.h"
#include "block_entities.h"
#include "build_run.h"
#include "brush_aim.h"
#include "build_validation.h"
#include "held_tool.h"
#include "station_build_phase.h"
#include "wall_bridge.h"
#include "wall_brush.h"
#include "wall_entities.h"
#include "wall_fit.h"
#include "wall_merge.h"
#include "wall_opening_brush.h"

namespace z13::building {

namespace {

using z13::station::WallId;

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
using z13::station::BrushLine;
using z13::station::BrushPreview;
using BuildKind = BrushPreview::Kind;
using z13::station::BuildPermission;
using z13::station::kCellSize;
using z13::station::SpawnPoint;
using z13::station::StationMode;
using z13::station::WallDraft;
using z13::station::WallBridgeDraft;
using z13::station::WallMergeDraft;
using z13::station::WallPanelOf;
using z13::station::WallNode;
using z13::station::WallSegment;

using BlockQuery = flecs::query<const Block>;
using PlayerQuery = flecs::query<const z13::gameplay::PlayerCollider, const Eigen::Matrix4f>;
using SpawnBlockQuery = flecs::query<const Block>;

void RegisterPipeline(flecs::world world) {
  world.component<WallPanelPhase>().add(flecs::Phase).depends_on<z13::building::UpdateBuildingToolPhase>();
  world.component<StationBuildPhase>()
      .add(flecs::Phase)
      .depends_on<z13::building::UpdateBuildingToolPhase>()
      .depends_on<WallPanelPhase>();
  world.component<z13::gameplay::PostUpdatePhase>().add(flecs::Phase).depends_on<StationBuildPhase>();
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<BlockIndex, BuildingTuning, BrushDrag, BuildPermission, BrushPreview,
                                       z13::station::Grid>(world);
}

// The main grid is a state entity, so leaving the station destroys it: every new scene makes it again.
void CreateMainGrid(flecs::world world) {
  const z13::ImmediateScope immediate(world);
  flecs::entity grid = world.entity(z13::station::kMainGridName.data());
  grid.add<z13::flecs_tools::StateEntity>().set(z13::station::Grid {});
  // An empty index from the start, as the singleton had; the first sync fills it.
  if (!grid.has<BlockIndex>()) {
    grid.set(BlockIndex {});
  }
}

void RegisterGridObserver(flecs::world world) {
  world.observer<const z13::gameplay::Gameplay>("BlockBuildingSystem::CreateMainGrid")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([](flecs::iter& it, size_t, const z13::gameplay::Gameplay&) { CreateMainGrid(it.world()); });
}

using GridQuery = flecs::query<const z13::station::Grid>;

// The entity of each grid, by the grid's id.
std::unordered_map<z13::station::GridId, flecs::entity> GridEntities(const GridQuery& grids) {
  std::unordered_map<z13::station::GridId, flecs::entity> entities;
  grids.each([&entities](flecs::entity entity, const z13::station::Grid& grid) { entities.emplace(grid.id, entity); });
  return entities;
}

// Every block into the index of its own grid, which is set whole once: the system defers sets. A block of a grid that
// does not exist is left out, and the reader's block count then never adds up: it resyncs every tick.
void RebuildIndexes(const BlockQuery& blocks, const GridQuery& grids, const BuildingTuning& tuning,
                    const Palette& palette) {
  Z13_PROFILE_ZONE("RebuildIndex");
  const auto entities = GridEntities(grids);
  std::unordered_map<z13::station::GridId, BlockIndex> indexes;
  for (const auto& [id, grid] : entities) {
    indexes.emplace(id, BlockIndex(tuning.index_chunk_cells));
  }
  blocks.each([&](flecs::entity e, const Block& block) {
    if (const auto index = indexes.find(block.grid); index != indexes.end()) {
      index->second.Insert(e.id(), ShapeOfBlock(block, palette));
    }
  });
  for (auto& [id, index] : indexes) {
    entities.at(id).set(std::move(index));
  }
}

// Build and destroy requests also update the index themselves, for later requests in the tick.
void SyncBlockIndexes(z13::station::BlockChangeReader& reader, uint64_t restores, uint64_t tick,
                      const GridQuery& grids, const BuildingTuning& tuning, const Palette& palette) {
  const auto entities = GridEntities(grids);
  size_t indexed {};
  bool rebuild {};
  for (const auto& [id, grid] : entities) {
    const auto* index = grid.try_get<BlockIndex>();
    indexed += index != nullptr ? index->Size() : 0;
    rebuild = rebuild || index == nullptr || index->ChunkCells() != tuning.index_chunk_cells;
  }
  const z13::station::BlockChangeBatch batch = reader.Read(restores, tick, indexed);
  if (batch.resync || rebuild) {
    RebuildIndexes(reader.Blocks(), grids, tuning, palette);
    return;
  }
  for (const z13::station::BlockChange& change : batch.changes) {
    const auto grid = entities.find(change.block.grid);
    if (grid == entities.end()) {
      continue;
    }
    BlockIndex& index = grid->second.get_mut<BlockIndex>();
    if (change.kind == z13::station::BlockChangeKind::kRemoved) {
      index.Erase(change.entity);
    } else {
      index.Insert(change.entity, ShapeOfBlock(change.block, palette));
    }
  }
}

// The aim of the player's brush in the grid its ray meets first (brush_aim.h).
std::optional<Aim> FindAim(flecs::entity player, const BuildGrids& grids) {
  std::optional<Aim> aim;
  if (grids.Empty()) {
    return aim;
  }
  player.children([&aim, player, &grids](flecs::entity child) {
    if (aim || !child.has<z13::building::Brush>() || !child.has<Eigen::Matrix4f>() ||
        !player.has<Eigen::Matrix4f>()) {
      return;
    }
    const Eigen::Vector3f eye = z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>());
    const Eigen::Vector3f reach = z13::math::ExtractTranslation<float>(child.get<Eigen::Matrix4f>());
    aim = AimAtGrids(grids, eye, reach);
  });
  return aim;
}

std::optional<BrushDrag> DragOf(flecs::entity player) {
  return player.has<BrushDrag>() ? std::optional(player.get<BrushDrag>()) : std::nullopt;
}

// What a build would place with the brush aimed at `aim`: the block dragged out from
// `drag`, or without one (or before it moved) the brush's block where it can rest (see BlockAt).
// When `kind` cuts the block goes into the aimed face, not against it, over what it cuts.
Block BrushBlock(const std::optional<BrushDrag>& dragged, const BlockSpec& spec, const Aim& aimed,
                 const BuildView& view, BuildKind kind) {
  // A drag begun in another grid is no drag here: its anchor is in other cells.
  const std::optional<BrushDrag> drag = dragged && dragged->anchor_grid == aimed.grid ? dragged : std::nullopt;
  const bool cutting = kind != BuildKind::kBuild;
  const Aim aim = cutting ? aimed.Flipped() : aimed;
  const Eigen::Vector3i anchor_cell = drag ? (cutting ? drag->anchor_cell - drag->anchor_normal : drag->anchor_cell)
                                           : Eigen::Vector3i::Zero();
  Block block;
  if (!drag || anchor_cell == aim.Cell()) {
    block = BlockAt(aim, spec, view.index, view.tuning.get().max_slide_cells, cutting);
  } else {
    const auto primitive = view.palette.get().Find(spec.type_id);
    block = z13::building::primitives::DraggedBlock(
        anchor_cell, aim.Cell(), spec, primitive ? primitive->get().min_size : spec.size,
        primitive ? primitive->get().max_size : spec.size, cutting ? -drag->anchor_normal : drag->anchor_normal,
        primitive ? primitive->get().shape : Shape {});
  }
  block.grid = aimed.grid;
  return block;
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

using ClearancesByGrid = std::unordered_map<z13::station::GridId, std::vector<CellBox>>;

ClearancesByGrid SpawnClearances(const SpawnBlockQuery& spawn_blocks, const BuildingTuning& tuning) {
  ClearancesByGrid clearances;
  spawn_blocks.each([&clearances, &tuning](const Block& marker) {
    clearances[marker.grid].push_back(SpawnClearance(marker, tuning));
  });
  return clearances;
}

void StartBrushDrag(flecs::entity player, RequestBrushDrag, const BuildGrids& grids) {
  player.remove<RequestBrushDrag>();
  if (const auto aim = FindAim(player, grids)) {
    player.set(BrushDrag {.anchor_cell = aim->Cell(), .anchor_normal = aim->normal, .anchor_grid = aim->grid});
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

WallPicking PickingOf(const BuildingTuning& tuning) {
  return {.snap_cells = tuning.wall_snap_cells,
          .reach_cells = tuning.wall_pick_reach_cells,
          .plane_slack_cells = tuning.wall_plane_slack_cells,
          .arrow_cells = tuning.wall_arrow_cells,
          .arrow_head_cells = tuning.wall_arrow_head_cells};
}

// A click of the Wall mode: it sets the draft's next point, and the third makes the wall
// from its nodes (existing ones where it snapped to them).
z13::gameplay::SpawnNamer& NamerOf(BuildRun& run, flecs::entity player) {
  return run.NamerOf(StampNow(player.world(), player.get<z13::gameplay::Player>().id));
}

void ProcessWallClick(flecs::entity player, flecs::entity tool, const Aim& aim, BuildRun& run) {
  flecs::world world = player.world();
  const std::optional<WallDraft> draft = DraftOf(player);
  const std::vector<Eigen::Vector3i> anchors = WallAnchors(world);
  const auto click = CheckPermission(player).and_then([&] {
    return ClickWall(draft, aim, anchors, PickingOf(run.tuning), tool.get<BuildTool>().wall_thickness);
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
  const auto valid = faced.and_then([&](const FacedWall& wall) { return CheckWallFits(world, wall.wall, run.View()); });
  if (!valid) {
    log_debug("station: wall refused: {}", valid.error());
    return;
  }
  SpawnWall(world, NamerOf(run, player), faced->wall);
  player.remove<WallDraft>();
}

std::optional<WallId> MergeDraftNode(flecs::entity player) {
  return player.has<WallMergeDraft>() ? std::optional(player.get<WallMergeDraft>().node_id) : std::nullopt;
}

// A click of the Merge mode: the first picks a wall node, the second merges it with another.
void ProcessMergeClick(flecs::entity player, flecs::entity tool, const Aim& aim, BuildRun& run) {
  flecs::world world = player.world();
  const std::optional<WallId> clicked = NearestWallNode(world, aim, run.tuning.get().wall_snap_cells);
  const std::optional<WallId> first = MergeDraftNode(player);
  const auto plan = CheckPermission(player).and_then([&]() -> std::expected<std::optional<NodeMerge>, std::string> {
    if (!clicked) {
      return std::unexpected(std::string {"no wall node there"});
    }
    if (!first) {
      return std::nullopt;
    }
    return PlanNodeMerge(
        world, *first, *clicked, tool.get<BuildTool>().merge_at, run.palette, run.tuning.get().wall_joint_max_reach_cells).transform(
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
  ApplyNodeMerge(world, NamerOf(run, player), **plan);
  player.remove<WallMergeDraft>();
}

std::optional<WallId> BridgeDraftNode(flecs::entity player) {
  return player.has<WallBridgeDraft>() ? std::optional(player.get<WallBridgeDraft>().node_id) : std::nullopt;
}

// The bridge from the stick at `first` to the one at `second` with the tool's thickness, if it
// can stand: the player may build and the wall fits (wall_fit.h).
std::expected<FacedWall, std::string> CheckedBridge(
    flecs::entity player, flecs::entity tool, WallId first, WallId second, const Aim& aim,
    const BuildView& view) {
  flecs::world world = player.world();
  const bool flipped = player.has<WallBridgeDraft>() && player.get<WallBridgeDraft>().flipped;
  return CheckPermission(player)
      .and_then([&] {
        return PlanBridge(world, first, second, tool.get<BuildTool>().wall_thickness, aim.eye, view.palette, flipped);
      })
      .and_then([&](const FacedWall& bridge) -> std::expected<FacedWall, std::string> {
        if (const auto fits = CheckWallFits(world, bridge.wall, view); !fits) {
          return std::unexpected(fits.error());
        }
        return bridge;
      });
}

// A click of the Bridge mode: the first picks a wall node, the second lays a wall to the node
// whose stick matches.
void ProcessBridgeClick(flecs::entity player, flecs::entity tool, const Aim& aim, BuildRun& run) {
  flecs::world world = player.world();
  const std::optional<WallId> clicked = NearestWallNode(world, aim, run.tuning.get().wall_snap_cells);
  const std::optional<WallId> first = BridgeDraftNode(player);
  const auto picked = CheckPermission(player).and_then([&]() -> Status {
    if (!clicked || SticksAt(world, *clicked).empty()) {
      return std::unexpected(std::string {"no wall node there"});
    }
    return {};
  });
  if (!picked) {
    log_debug("station: bridge click refused: {}", picked.error());
    return;
  }
  if (!first) {
    player.set(WallBridgeDraft {.node_id = *clicked});
    return;
  }
  const auto bridge = CheckedBridge(player, tool, *first, *clicked, aim, run.View());
  if (!bridge) {
    log_debug("station: bridge refused: {}", bridge.error());
    return;
  }
  SpawnWall(world, NamerOf(run, player), bridge->wall);
  player.remove<WallBridgeDraft>();
}

// The player may build and the wall's pieces fit (wall_fit.h).
Status OpeningAllowed(flecs::entity player, const z13::station::WallOpening& opening, const BuildView& view) {
  return CheckPermission(player).and_then([&] { return CheckOpeningFits(player.world(), opening, view); });
}

void ProcessOpeningClick(flecs::entity player, flecs::entity tool, const Aim& aim, BuildRun& run) {
  const std::optional<AimedOpening> aimed =
      AimOpening(player.world(), aim, tool.get<BuildTool>(), run.tuning.get());
  if (!aimed) {
    log_debug("station: opening refused: no wall there");
    return;
  }
  if (const Status allowed = OpeningAllowed(player, aimed->opening, run.View()); !allowed) {
    log_debug("station: opening refused: {}", allowed.error());
    return;
  }
  SpawnWallOpening(player.world(), NamerOf(run, player), aimed->opening);
}

// The modes whose walls, nodes and openings live in the main grid only.
bool UsesWalls(ToolMode mode) {
  return mode == ToolMode::kWall || mode == ToolMode::kMerge || mode == ToolMode::kBridge ||
         mode == ToolMode::kOpening;
}

// A dragged block's size becomes the brush's, so the next click repeats it. With CutModifier
// the block's cells are cut out of the station instead; with CutInModifier they are cut
// out first and the block takes their place.
void ProcessBuildRequest(flecs::entity player, BuildRun& run, const BuildGrids& grids) {
  player.remove<z13::building::RequestBuildBlock>();
  const std::optional<flecs::entity> tool = z13::building::HeldBuildTool(player);
  if (!tool || !tool->has<BlockBrush>()) {
    player.remove<BrushDrag>();
    return;
  }
  const auto aim = FindAim(player, grids);
  if (aim) {
    run.Select(grids.Find(aim->grid)->get());
  }
  if (aim && aim->grid != z13::station::kMainGrid && UsesWalls(tool->get<BuildTool>().mode)) {
    log_debug("station: the wall tools work on the main grid only");
    return;
  }
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
  if (tool->get<BuildTool>().mode == ToolMode::kBridge) {
    if (aim) {
      ProcessBridgeClick(player, *tool, *aim, run);
    }
    return;
  }
  if (tool->get<BuildTool>().mode == ToolMode::kOpening) {
    if (aim) {
      ProcessOpeningClick(player, *tool, *aim, run);
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
    ApplyCut(world, *plan, run, NamerOf(run, player));
    run.spawn_points -= plan->spawn_points_removed;
  }
  if (kind != BuildKind::kCut) {
    const flecs::entity created =
        CreateBlock(world, NamerOf(run, player).NextName("Block"), block, run.palette, run.tuning);
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
  const BuildGrids grids = BuildGrids::Of(it.world());
  while (it.next()) {
    if (grids.Empty()) {
      continue;
    }
    if (!run) {
      const auto& tuning = it.field<const BuildingTuning>(3)[0];
      run = BuildRun {
          .index = grids.Fallback().MutableIndex(),
          .palette = it.field<const BlockPalette>(2)[0].palette,
          .tuning = tuning,
          .players = PlayerSpheres(players),
          .clearances_by_grid = SpawnClearances(spawn_blocks, tuning),
          .spawn_points = it.world().count<SpawnPoint>()};
    }
    auto& actor = it.field<z13::gameplay::EditActor>(4)[0];
    for (const size_t i : it) {
      const flecs::entity player = it.entity(i);
      const z13::gameplay::ActorScope scope(actor, player.get<z13::gameplay::Player>().id);
      ProcessBuildRequest(player, *run, grids);
    }
  }
}

// In the Opening mode destroying takes the opening aimed at, gap included, and leaves its wall.
bool DestroyAimedOpening(flecs::entity player, const BuildGrids& grids) {
  const std::optional<flecs::entity> tool = z13::building::HeldBuildTool(player);
  if (!tool || !tool->has<BuildTool>() || tool->get<BuildTool>().mode != ToolMode::kOpening) {
    return false;
  }
  const auto aim = FindAim(player, grids);
  const auto opening =
      aim && aim->grid == z13::station::kMainGrid ? OpeningUnder(player.world(), *aim) : std::nullopt;
  if (opening) {
    z13::gameplay::RecordStateChange(
        *opening, z13::gameplay::StateChangeKind::kRemoved,
        WallSegmentCell(player.world(), opening->get<z13::station::WallOpening>().segment_id),
        WallSegmentGrid(player.world(), opening->get<z13::station::WallOpening>().segment_id));
    opening->destruct();
  }
  return opening.has_value();
}

// A run() rather than each(): the spawn point count carries across requests, so two
// players can't each remove one of the last two in the same tick.
void ProcessDestroyRequests(flecs::iter& it) {
  int spawn_points = it.world().count<SpawnPoint>();
  const BuildGrids grids = BuildGrids::Of(it.world());
  while (it.next()) {
    if (grids.Empty()) {
      continue;
    }
    const auto transforms = it.field<const Eigen::Matrix4f>(2);
    const auto& palette = it.field<const BlockPalette>(3)[0];
    const auto& tuning = it.field<const BuildingTuning>(4)[0];
    auto& actor = it.field<z13::gameplay::EditActor>(5)[0];
    for (const size_t i : it) {
      flecs::entity player = it.entity(i);
      const z13::gameplay::ActorScope scope(actor, player.get<z13::gameplay::Player>().id);
      player.remove<z13::building::RequestDestroyBlock>();
      if (const auto permitted = CheckPermission(player); !permitted) {
        log_debug("station: destroy refused: {}", permitted.error());
        continue;
      }

      if (DestroyAimedOpening(player, grids)) {
        continue;
      }
      const Eigen::Vector3f origin = z13::math::ExtractTranslation<float>(transforms[i]);
      const Eigen::Vector3f forward = transforms[i].block<3, 3>(0, 0).col(0);
      const auto hit = grids.Raycast(origin, origin + forward * tuning.destroy_reach_distance);
      if (!hit) {
        continue;
      }
      const flecs::entity target = it.world().entity(hit->hit.block);
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
      hit->grid.get().MutableIndex().Erase(hit->hit.block);
      z13::station::DestroyBlock(target, block);
    }
  }
}

BrushPreview WallBrushPreview(flecs::entity owner, flecs::entity tool, const Aim& aim, const BuildView& view) {
  const std::optional<WallDraft> draft = DraftOf(owner);
  const std::vector<Eigen::Vector3i> anchors = WallAnchors(owner.world());
  const WallPreview preview = PreviewWall(
      draft, aim, anchors, PickingOf(view.tuning), tool.get<BuildTool>().wall_thickness, view.palette);
  const bool valid = preview.makeable && CheckPermission(owner).has_value() &&
                     (!preview.wall || CheckWallFits(owner.world(), *preview.wall, view).has_value());
  return {.block = preview.block, .valid = valid, .kind = BuildKind::kBuild, .lines = preview.lines,
          .parts = preview.parts};
}

// What the Merge and Bridge modes show of their nodes: the one picked first as a marker, and the
// sticks of that one and of the one aimed at.
void AddNodeMarks(
    flecs::world world, const std::optional<WallId>& first, const std::optional<WallId>& hovered,
    BrushPreview& preview) {
  for (const std::optional<WallId>& node : {first, hovered != first ? hovered : std::nullopt}) {
    if (node) {
      const std::vector<BrushLine> sticks = StickLinesAt(world, *node);
      preview.lines.insert(preview.lines.end(), sticks.begin(), sticks.end());
    }
  }
  if (const auto cell = first ? WallNodeCell(world, *first) : std::nullopt) {
    preview.parts.push_back(CellMarker(*cell));
  }
}

BrushPreview MergeBrushPreview(flecs::entity owner, flecs::entity tool, const Aim& aim, const BuildView& view) {
  flecs::world world = owner.world();
  const std::optional<WallId> hovered = NearestWallNode(world, aim, view.tuning.get().wall_snap_cells);
  const std::optional<WallId> first = MergeDraftNode(owner);
  const std::optional<Eigen::Vector3i> cell = hovered ? WallNodeCell(world, *hovered) : std::nullopt;
  bool valid = hovered.has_value() && CheckPermission(owner).has_value();
  Eigen::Vector3i shown = cell.value_or(aim.point.array().round().cast<int>().matrix());
  if (valid && first) {
    const auto plan = PlanNodeMerge(
        world, *first, *hovered, tool.get<BuildTool>().merge_at, view.palette, view.tuning.get().wall_joint_max_reach_cells);
    valid = plan.has_value();
    shown = plan ? plan->cell : shown;
  }
  BrushPreview preview {.block = CellMarker(shown), .valid = valid, .kind = BuildKind::kBuild};
  AddNodeMarks(world, first, hovered, preview);
  return preview;
}

BrushPreview BridgeBrushPreview(flecs::entity owner, flecs::entity tool, const Aim& aim, const BuildView& view) {
  flecs::world world = owner.world();
  const std::optional<WallId> hovered = NearestWallNode(world, aim, view.tuning.get().wall_snap_cells);
  const std::optional<WallId> first = BridgeDraftNode(owner);
  const std::optional<Eigen::Vector3i> cell = hovered ? WallNodeCell(world, *hovered) : std::nullopt;
  const Eigen::Vector3i shown = cell.value_or(aim.point.array().round().cast<int>().matrix());
  BrushPreview preview {.block = CellMarker(shown), .valid = false, .kind = BuildKind::kBuild};
  if (first && hovered) {
    if (const auto bridge = CheckedBridge(owner, tool, *first, *hovered, aim, view)) {
      preview.block = bridge->blocks.front();
      preview.valid = true;
      preview.parts = {bridge->blocks.begin() + 1, bridge->blocks.end()};
    }
  } else {
    preview.valid = hovered && !SticksAt(world, *hovered).empty() && CheckPermission(owner).has_value();
  }
  AddNodeMarks(world, first, hovered, preview);
  return preview;
}

BrushPreview OpeningBrushPreview(flecs::entity owner, flecs::entity tool, const Aim& aim, const BuildView& view) {
  BrushPreview preview {
      .block = CellMarker(aim.point.array().round().cast<int>().matrix()), .valid = false, .kind = BuildKind::kBuild};
  if (const auto aimed = AimOpening(owner.world(), aim, tool.get<BuildTool>(), view.tuning.get())) {
    preview.lines = aimed->lines;
    preview.valid = OpeningAllowed(owner, aimed->opening, view).has_value();
  }
  return preview;
}

// What the preview is judged against, apart from the grid it is aimed at.
struct PreviewContext {
  std::reference_wrapper<const Palette> palette;
  std::reference_wrapper<const BuildingTuning> tuning;
  std::reference_wrapper<const BuildGrids> grids;
  std::span<const PlayerSphere> players;  // in the world, meters
  std::reference_wrapper<const ClearancesByGrid> clearances;
};

void UpdateBrushPreview(flecs::entity brush, const PreviewContext& context) {
  const flecs::entity owner = brush.parent();
  const std::optional<flecs::entity> tool = owner ? z13::building::HeldBuildTool(owner) : std::nullopt;
  if (!tool || !tool->has<BlockBrush>()) {
    return;
  }
  const auto aim = FindAim(owner, context.grids);
  if (!aim) {
    return;
  }
  if (aim->grid != z13::station::kMainGrid && UsesWalls(tool->get<BuildTool>().mode)) {
    return;
  }
  const GridView& target = context.grids.get().Find(aim->grid)->get();
  std::vector<PlayerSphere> local_players;
  for (const PlayerSphere& player : context.players) {
    local_players.push_back(
        {.center = target.ToLocalMeters(player.center), .radius = player.radius});
  }
  const auto clearances = context.clearances.get().find(aim->grid);
  const BuildView view {
      .palette = context.palette, .index = target.Index(), .tuning = context.tuning, .players = local_players,
      .spawn_clearances = clearances != context.clearances.get().end() ? std::span<const CellBox>(clearances->second)
                                                                       : std::span<const CellBox> {}};
  if (tool->get<BuildTool>().mode == ToolMode::kWall) {
    brush.set(WallBrushPreview(owner, *tool, *aim, view));
    return;
  }
  if (tool->get<BuildTool>().mode == ToolMode::kMerge) {
    brush.set(MergeBrushPreview(owner, *tool, *aim, view));
    return;
  }
  if (tool->get<BuildTool>().mode == ToolMode::kBridge) {
    brush.set(BridgeBrushPreview(owner, *tool, *aim, view));
    return;
  }
  if (tool->get<BuildTool>().mode == ToolMode::kOpening) {
    brush.set(OpeningBrushPreview(owner, *tool, *aim, view));
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

void RegisterSystems(flecs::world world) {
  const auto reader = std::make_shared<z13::station::BlockChangeReader>(world);
  const GridQuery grids = world.query<const z13::station::Grid>();
  world.system<const z13::flecs_tools::WorldRestores, const z13::flecs_tools::SimulationClock, const BuildingTuning,
               const BlockPalette>("BlockBuildingSystem::SyncBlockIndex")
      .kind<StationBuildPhase>()
      .with<StationMode>()
      .read<Block>()
      .read<z13::station::BlockChange>()
      .read<z13::station::Grid>()
      .write<BlockIndex>()
      .each([reader, grids](flecs::iter&, size_t, const z13::flecs_tools::WorldRestores& restores,
                            const z13::flecs_tools::SimulationClock& clock, const BuildingTuning& tuning,
                            const BlockPalette& palette) {
        SyncBlockIndexes(*reader, restores.count, clock.tick, grids, tuning, palette.palette);
      });

  world.system<RequestBrushDrag>("BlockBuildingSystem::StartBrushDrag")
      .kind<StationBuildPhase>()
      .with<StationMode>()
      .read<BlockIndex>()
      .write<BrushDrag>()
      .each([](flecs::entity player, RequestBrushDrag request) {
        const BuildGrids grids = BuildGrids::Of(player.world());
        if (!grids.Empty()) {
          StartBrushDrag(player, request, grids);
        }
      });

  const PlayerQuery players =
      world.query_builder<const z13::gameplay::PlayerCollider, const Eigen::Matrix4f>("BlockBuildingSystem::Players")
          .with<z13::gameplay::Player>()
          .build();
  const SpawnBlockQuery spawn_blocks =
      world.query_builder<const Block>("BlockBuildingSystem::SpawnBlocks").with<SpawnPoint>().build();

  // Same-tick requests compete, so they go in player id order (see CompareByPlayerId).
  world.system<z13::building::RequestBuildBlock, const z13::gameplay::Player, const BlockPalette, const BuildingTuning,
               z13::gameplay::EditActor>("BlockBuildingSystem::ProcessBuildRequests")
      .kind<StationBuildPhase>()
      .term_at(4).inout()
      .with<StationMode>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .read<z13::inventory::ActiveItem>()
      .write<BlockBrush>()
      .read<BuildPermission>()
      .read<z13::building::CutModifier>()
      .read<z13::building::CutInModifier>()
      .write<BlockIndex>()
      .write<BrushDrag>()
      .write<Block>()
      .write<z13::station::BlockChange>()
      .write<SpawnPoint>()
      .write<WallDraft>()
      .write<WallMergeDraft>()
      .write<WallBridgeDraft>()
      .write<WallNode>()
      .write<WallSegment>()
      .write<z13::station::WallOpening>()
      .run([players, spawn_blocks](flecs::iter& it) { ProcessBuildRequests(it, players, spawn_blocks); });

  world.system<z13::building::RequestDestroyBlock, const z13::gameplay::Player, const Eigen::Matrix4f,
               const BlockPalette, const BuildingTuning, z13::gameplay::EditActor>(
      "BlockBuildingSystem::ProcessDestroyRequests")
      .kind<StationBuildPhase>()
      .term_at(5).inout()
      .with<StationMode>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .read<BuildPermission>()
      .read<SpawnPoint>()
      .read<WallPanelOf>()
      .write<BlockIndex>()
      .write<Block>()
      .write<z13::station::BlockChange>()
      .write<z13::station::WallSegment>()
      .write<z13::station::WallNode>()
      .write<z13::station::WallOpening>()
      .run(ProcessDestroyRequests);

  // Last, so the preview shows this frame's blocks; the render draws it in OnStore.
  world.system<const Eigen::Matrix4f, const BlockPalette, const BuildingTuning>(
           "BlockBuildingSystem::UpdateBrushPreview")
      .kind<StationBuildPhase>()
      .with<z13::building::Brush>()
      .with<StationMode>()
      .read<z13::inventory::ActiveItem>()
      .read<BlockBrush>()
      .read<Block>()
      .read<z13::building::CutModifier>()
      .read<z13::building::CutInModifier>()
      .read<BlockIndex>()
      .read<BrushDrag>()
      .read<BuildPermission>()
      .read<WallDraft>()
      .read<WallMergeDraft>()
      .read<WallBridgeDraft>()
      .read<WallNode>()
      .read<WallSegment>()
      .read<z13::station::WallOpening>()
      .write<BrushPreview>()
      .each([players, spawn_blocks](flecs::entity brush, const Eigen::Matrix4f&, const BlockPalette& palette,
                                    const BuildingTuning& tuning) {
        const BuildGrids grids = BuildGrids::Of(brush.world());
        if (grids.Empty()) {
          return;
        }
        const std::vector<PlayerSphere> spheres = PlayerSpheres(players);
        const ClearancesByGrid clearances = SpawnClearances(spawn_blocks, tuning);
        UpdateBrushPreview(brush, {.palette = palette.palette, .tuning = tuning, .grids = grids,
                                   .players = spheres, .clearances = clearances});
      });
}

}  // namespace

void BlockBuildingSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipeline);

  OnInitSystems(world, RegisterSystems);

  OnInitWorldData(world, CreateMainGrid);

  OnInitSystems(world, RegisterGridObserver);
}

}  // namespace z13::building
