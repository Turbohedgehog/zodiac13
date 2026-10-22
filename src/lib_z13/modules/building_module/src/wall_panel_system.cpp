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

#include <map>
#include <unordered_map>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/block.h>
#include <z13/components/block_changes.h>
#include <z13/components/gameplay.h>
#include <z13/components/station_mode.h>
#include <z13/components/wall.h>
#include <primitives/palette.h>
#include <primitives/wall_panel.h>
#include <primitives/wall_seam.h>
#include <z13_settings/building_tuning.h>

#include "block_entities.h"
#include "station_build_phase.h"
#include "wall_entities.h"
#include "wall_joints.h"

namespace z13::building {

namespace {

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
  uint32_t segment_id {};
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
  std::unordered_map<uint32_t, Eigen::Vector3i> cells;
  nodes.each([&cells](const WallNode& node) { cells.emplace(node.id, node.cell); });
  std::unordered_map<uint32_t, std::vector<z13::station::WallOpening>> cut;
  openings.each([&cut](const z13::station::WallOpening& opening) { cut[opening.segment_id].push_back(opening); });
  std::vector<JointWall> walls;
  segments.each([&](const WallSegment& segment) {
    const auto a = cells.find(segment.node_a);
    const auto b = cells.find(segment.node_b);
    if (a != cells.end() && b != cells.end()) {
      walls.push_back(JointWallOf(segment, a->second, b->second, cut[segment.id]));
    }
  });
  const WallJoints joints = SolveJoints(walls, joint_reach);
  std::map<PanelKey, Block> expected;
  for (const JointWall& wall : walls) {
    const auto blocks = JointPanels(wall, joints, palette);
    if (!blocks) {
      log_debug("station: wall segment {} has no panel: {}", wall.id, blocks.error());
      continue;
    }
    for (uint32_t part = 0; part < blocks->size(); ++part) {
      expected.emplace(PanelKey {.segment_id = wall.id, .part = part}, (*blocks)[part]);
    }
  }
  return expected;
}

void SyncPanels(
    flecs::world world, const flecs::query<const WallNode>& nodes, const flecs::query<const WallSegment>& segments,
    const flecs::query<const z13::station::WallOpening>& openings,
    const flecs::query<const WallPanelOf, const Block>& panels, z13::gameplay::IdCounters& counters,
    const BlockPalette& palette, const BuildingTuning& tuning) {
  const std::map<PanelKey, Block> expected =
      ExpectedPanels(nodes, segments, openings, palette.palette, tuning.wall_joint_max_reach_cells);

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
  for (const auto& [key, block] : expected) {
    if (!existing.contains(key)) {
      z13::station::SpawnBlock(world.entity(NextBlockName(world, counters).c_str()), block)
          .set(WallPanelOf {.segment_id = key.segment_id, .part = key.part});
    }
  }
}

void RegisterSystems(flecs::world world) {
  const auto nodes = world.query<const WallNode>("WallPanelSystem::Nodes");
  const auto segments = world.query<const WallSegment>("WallPanelSystem::Segments");
  const auto openings = world.query<const z13::station::WallOpening>("WallPanelSystem::Openings");
  const auto panels = world.query<const WallPanelOf, const Block>("WallPanelSystem::Panels");
  world.system<z13::gameplay::IdCounters, const BlockPalette, const BuildingTuning>("WallPanelSystem::SyncPanels")
      .kind<WallPanelPhase>()
      .with<StationMode>()
      .read<WallNode>()
      .read<WallSegment>()
      .read<z13::station::WallOpening>()
      .read<WallPanelOf>()
      .write<Block>()
      .write<WallPanelOf>()
      .write<z13::station::BlockChange>()
      .each([nodes, segments, openings, panels](
                flecs::iter& it, size_t, z13::gameplay::IdCounters& counters, const BlockPalette& palette,
                const BuildingTuning& tuning) {
        SyncPanels(it.world(), nodes, segments, openings, panels, counters, palette, tuning);
      });
}

}  // namespace

void WallPanelSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
