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

#include "wall_panel_system.h"

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/station/block.h>
#include <z13/components/station/block_changes.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/grid_pose.h>
#include <z13/components/station/held_sectors.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/station/station_mode.h>
#include <z13/components/station/wall.h>
#include <primitives/palette.h>
#include <primitives/wall_panel.h>
#include <primitives/wall_seam.h>
#include <z13_settings/building_tuning.h>

#include "block_entities.h"
#include "held_blocks.h"
#include "station_build_phase.h"
#include "wall_panel_inputs.h"
#include "wall_entities.h"
#include "wall_joints.h"

namespace z13::building {

namespace {

using z13::station::WallId;

using z13::building::primitives::BlockPalette;
using z13::station::Block;
using z13::station::StationMode;
using z13::station::WallNode;
using z13::station::WallPanelOf;
using z13::station::WallSegment;

struct Panel {
  flecs::entity entity;
  Block block;
};

// One of a segment's panels: its single one, or either triangle of a bent wall.
struct PanelKey {
  WallId segment_id {};
  uint32_t part {};

  auto operator<=>(const PanelKey&) const = default;
};

void RegisterComponents(flecs::world world) {
  // WallStick first: WallSegment holds two.
  z13::flecs_tools::RegisterComponentMeta<z13::station::WallStick>(world);
  z13::flecs_tools::RegisterComponents<WallNode, WallSegment, z13::station::WallOpening, WallPanelOf,
                                       z13::station::WallDraft, z13::station::WallMergeDraft,
                                       z13::station::WallBridgeDraft>(world);
}

// The panels each segment should have; a segment that can't have them has none. Walls meeting
// two at a node end along one seam, or square where that can't be had.
std::map<PanelKey, Block> ExpectedPanels(
    const flecs::query<const WallNode>& nodes, const flecs::query<const WallSegment>& segments,
    const flecs::query<const z13::station::WallOpening>& openings, const z13::building::primitives::Palette& palette,
    int joint_reach) {
  std::unordered_map<WallId, Eigen::Vector3i> cells;
  std::unordered_map<WallId, z13::station::GridId> grids;
  nodes.each([&cells, &grids](const WallNode& node) {
    cells.emplace(node.id, node.cell);
    grids.emplace(node.id, node.grid);
  });
  std::unordered_map<WallId, std::vector<z13::station::WallOpening>> cut;
  openings.each([&cut](const z13::station::WallOpening& opening) { cut[opening.segment_id].push_back(opening); });
  // Walls meet only within one grid: their cells are the grid's own.
  std::map<z13::station::GridId, std::vector<JointWall>> walls_by_grid;
  segments.each([&](const WallSegment& segment) {
    const auto a = cells.find(segment.node_a);
    const auto b = cells.find(segment.node_b);
    if (a != cells.end() && b != cells.end()) {
      walls_by_grid[grids.at(segment.node_a)].push_back(JointWallOf(segment, a->second, b->second, cut[segment.id]));
    }
  });
  std::map<PanelKey, Block> expected;
  for (const auto& [grid, walls] : walls_by_grid) {
    const WallJoints joints = SolveJoints(walls, joint_reach);
    for (const JointWall& wall : walls) {
      const auto blocks = JointPanels(wall, joints, palette);
      if (!blocks) {
        log_debug("station: wall segment {} has no panel: {}", wall.id, blocks.error());
        continue;
      }
      for (uint32_t part = 0; part < blocks->size(); ++part) {
        Block panel = (*blocks)[part];
        panel.grid = grid;
        expected.emplace(PanelKey {.segment_id = wall.id, .part = part}, panel);
      }
    }
  }
  return expected;
}

// A client under interest holds the blocks of its sectors and gets those by group; the wall segments reach it whole,
// so it must not derive the panels of the rest (they would be named by its own tick, apart from the server's).
void KeepHeld(std::map<PanelKey, Block>& expected, const z13::station::HeldSectors& held,
              const z13::station::GridPoses& poses) {
  std::erase_if(expected, [&](const auto& entry) { return !TouchesHeldSectors(entry.second, held, poses); });
}

PanelInputs InputsOf(const flecs::query<const WallNode>& nodes, const flecs::query<const WallSegment>& segments,
                     const flecs::query<const z13::station::WallOpening>& openings,
                     const flecs::query<const WallPanelOf, const Block>& panels,
                     const flecs::query<const z13::station::Grid>& grids, const z13::station::HeldSectors& held,
                     int joint_reach) {
  PanelInputs inputs {.held = held, .joint_reach = joint_reach};
  nodes.each([&inputs](const WallNode& node) { inputs.nodes.push_back(node); });
  segments.each([&inputs](const WallSegment& segment) { inputs.segments.push_back(segment); });
  openings.each([&inputs](const z13::station::WallOpening& opening) { inputs.openings.push_back(opening); });
  panels.each([&inputs](flecs::entity entity, const WallPanelOf& of, const Block& block) {
    inputs.panels.push_back({.entity = entity.id(), .of = of, .block = block});
  });
  grids.each([&inputs](const z13::station::Grid& grid) { inputs.grids.push_back(grid); });
  // The query walks archetypes, and a panel that gains a component elsewhere (a body) moves to another one.
  std::ranges::sort(inputs.panels, {}, &PanelEntity::entity);
  return inputs;
}

void SyncPanels(
    flecs::world world, const flecs::query<const WallNode>& nodes, const flecs::query<const WallSegment>& segments,
    const flecs::query<const z13::station::WallOpening>& openings,
    const flecs::query<const WallPanelOf, const Block>& panels, const flecs::query<const z13::station::Grid>& grids,
    const BlockPalette& palette, const BuildingTuning& tuning, const z13::station::HeldSectors& held,
    std::optional<PanelInputs>& last) {
  // Deriving every panel costs tens of ms in Debug, so it waits for a change in what they derive from; the panels
  // there are among the inputs, so one that is missing or changed by anyone else is derived again.
  PanelInputs inputs = InputsOf(nodes, segments, openings, panels, grids, held, tuning.wall_joint_max_reach_cells);
  if (last == inputs) {
    return;
  }
  last = std::move(inputs);
  std::map<PanelKey, Block> expected =
      ExpectedPanels(nodes, segments, openings, palette.palette, tuning.wall_joint_max_reach_cells);
  KeepHeld(expected, held, z13::station::PosesOf(grids));

  std::map<PanelKey, Panel> existing;
  std::vector<Panel> stale;
  panels.each([&](flecs::entity entity, const WallPanelOf& of, const Block& block) {
    const PanelKey key {.segment_id = of.segment_id, .part = of.part};
    const auto wanted = expected.find(key);
    if (wanted != expected.end() && wanted->second == block && !existing.contains(key)) {
      existing.emplace(key, Panel {entity, block});
    } else {
      stale.push_back({entity, block});
    }
  });
  for (const Panel& panel : stale) {
    z13::station::DestroyBlock(panel.entity, panel.block);
  }
  const uint64_t tick = world.get<z13::flecs_tools::SimulationClock>().tick;
  for (const auto& [key, block] : expected) {
    if (!existing.contains(key)) {
      // Named by the panel and the tick, since the one it replaces may still be there (its destruction is deferred).
      const std::string name = std::format("WallPanel_{}_{}_{}", key.segment_id, key.part, tick);
      z13::station::SpawnBlock(world.entity(name.c_str()), block)
          .set(WallPanelOf {.segment_id = key.segment_id, .part = key.part});
    }
  }
}

void RegisterSystems(flecs::world world) {
  const auto nodes = world.query<const WallNode>("WallPanelSystem::Nodes");
  const auto segments = world.query<const WallSegment>("WallPanelSystem::Segments");
  const auto openings = world.query<const z13::station::WallOpening>("WallPanelSystem::Openings");
  const auto panels = world.query<const WallPanelOf, const Block>("WallPanelSystem::Panels");
  const auto grids = world.query<const z13::station::Grid>("WallPanelSystem::Grids");
  const auto last = std::make_shared<std::optional<PanelInputs>>();
  world.system<const BlockPalette, const BuildingTuning, const z13::station::HeldSectors>("WallPanelSystem::SyncPanels")
      .kind<WallPanelPhase>()
      .with<StationMode>()
      .read<WallNode>()
      .read<WallSegment>()
      .read<z13::station::WallOpening>()
      .read<WallPanelOf>()
      .read<z13::station::Grid>()
      .write<Block>()
      .write<WallPanelOf>()
      .write<z13::station::BlockChange>()
      .each([nodes, segments, openings, panels, grids, last](flecs::iter& it, size_t, const BlockPalette& palette,
                                                                const BuildingTuning& tuning,
                                                                const z13::station::HeldSectors& held) {
        SyncPanels(it.world(), nodes, segments, openings, panels, grids, palette, tuning, held, *last);
      });
}

}  // namespace

void WallPanelSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
