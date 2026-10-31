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

#include <primitives_generated.h>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/status.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <lib_core/state/state_markers.h>

#include <z13/components/block_change_reader.h>
#include <z13/components/block_changes.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/gameplay_phases.h>
#include <z13/components/grid.h>
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

void CreateMainGrid(flecs::world world) {
  world.entity(std::string(z13::station::kMainGridName).c_str())
      .add<z13::flecs_tools::StateEntity>()
      .set(z13::station::Grid {});
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

WallPicking PickingOf(const BuildingTuning& tuning) {
  return {.snap_cells = tuning.wall_snap_cells, .reach_cells = tuning.wall_pick_reach_cells};
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
  while (it.next()) {
    if (!run) {
      const auto& tuning = it.field<const BuildingTuning>(4)[0];
      run = BuildRun {
          .index = it.field<BlockIndex>(2)[0],
          .palette = it.field<const BlockPalette>(3)[0].palette,
          .tuning = tuning,
          .players = PlayerSpheres(players),
          .spawn_clearances = SpawnClearances(spawn_blocks, tuning),
          .spawn_points = it.world().count<SpawnPoint>()};
    }
    auto& actor = it.field<z13::gameplay::EditActor>(5)[0];
    for (const size_t i : it) {
      const flecs::entity player = it.entity(i);
      const z13::gameplay::ActorScope scope(actor, player.get<z13::gameplay::Player>().id);
      ProcessBuildRequest(player, *run);
    }
  }
}

// In the Opening mode destroying takes the opening aimed at, gap included, and leaves its wall.
bool DestroyAimedOpening(flecs::entity player, const BlockIndex& index) {
  const std::optional<flecs::entity> tool = z13::building::HeldBuildTool(player);
  if (!tool || !tool->has<BuildTool>() || tool->get<BuildTool>().mode != ToolMode::kOpening) {
    return false;
  }
  const auto aim = FindAim(player, index);
  const auto opening = aim ? OpeningUnder(player.world(), *aim) : std::nullopt;
  if (opening) {
    z13::gameplay::RecordStateChange(
        *opening, z13::gameplay::StateChangeKind::kRemoved,
        WallSegmentCell(player.world(), opening->get<z13::station::WallOpening>().segment_id));
    opening->destruct();
  }
  return opening.has_value();
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
    auto& actor = it.field<z13::gameplay::EditActor>(6)[0];
    for (const size_t i : it) {
      flecs::entity player = it.entity(i);
      const z13::gameplay::ActorScope scope(actor, player.get<z13::gameplay::Player>().id);
      player.remove<z13::building::RequestDestroyBlock>();
      if (const auto permitted = CheckPermission(player); !permitted) {
        log_debug("station: destroy refused: {}", permitted.error());
        continue;
      }

      if (DestroyAimedOpening(player, index)) {
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
               const BlockPalette, const BuildingTuning, z13::gameplay::EditActor>(
      "BlockBuildingSystem::ProcessBuildRequests")
      .kind<StationBuildPhase>()
      .term_at(2).inout()
      .term_at(5).inout()
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
      .write<WallBridgeDraft>()
      .write<WallNode>()
      .write<WallSegment>()
      .write<z13::station::WallOpening>()
      .run([players, spawn_blocks](flecs::iter& it) { ProcessBuildRequests(it, players, spawn_blocks); });

  world.system<z13::building::RequestDestroyBlock, const z13::gameplay::Player, const Eigen::Matrix4f, BlockIndex,
               const BlockPalette, const BuildingTuning, z13::gameplay::EditActor>(
      "BlockBuildingSystem::ProcessDestroyRequests")
      .kind<StationBuildPhase>()
      .term_at(3).inout()
      .term_at(6).inout()
      .with<StationMode>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .read<BuildPermission>()
      .read<SpawnPoint>()
      .read<WallPanelOf>()
      .write<Block>()
      .write<z13::station::BlockChange>()
      .write<z13::station::WallSegment>()
      .write<z13::station::WallNode>()
      .write<z13::station::WallOpening>()
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
      .read<WallBridgeDraft>()
      .read<WallNode>()
      .read<WallSegment>()
      .read<z13::station::WallOpening>()
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

  OnInitWorldData(world, CreateMainGrid);
  OnInitWorldData(world, CreateMainGrid);
}

}  // namespace z13::building
