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

#include "block_entities.h"
#include "station_build_phase.h"

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

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<WallNode, WallSegment, WallPanelOf>(world);
}

// The panel each segment should have, by segment id; a segment that can't have one has none.
std::map<uint32_t, Block> ExpectedPanels(
    const flecs::query<const WallNode>& nodes, const flecs::query<const WallSegment>& segments,
    const z13::building::primitives::Palette& palette) {
  std::unordered_map<uint32_t, Eigen::Vector3i> cells;
  nodes.each([&cells](const WallNode& node) { cells.emplace(node.id, node.cell); });
  std::map<uint32_t, Block> expected;
  segments.each([&](const WallSegment& segment) {
    const auto a = cells.find(segment.node_a);
    const auto b = cells.find(segment.node_b);
    if (a == cells.end() || b == cells.end()) {
      return;
    }
    const auto block = z13::building::primitives::WallPanelBlock(
        {.node_a = a->second,
         .node_b = b->second,
         .stick_axis = segment.stick_axis,
         .stick_length = segment.stick_length,
         .thickness = segment.thickness},
        palette);
    if (block) {
      expected.emplace(segment.id, *block);
    } else {
      log_debug("station: wall segment {} has no panel: {}", segment.id, block.error());
    }
  });
  return expected;
}

void SyncPanels(
    flecs::world world, const flecs::query<const WallNode>& nodes, const flecs::query<const WallSegment>& segments,
    const flecs::query<const WallPanelOf, const Block>& panels, z13::gameplay::IdCounters& counters,
    const BlockPalette& palette) {
  const std::map<uint32_t, Block> expected = ExpectedPanels(nodes, segments, palette.palette);

  std::map<uint32_t, Panel> existing;
  std::vector<Panel> stale;
  panels.each([&](flecs::entity entity, const WallPanelOf& of, const Block& block) {
    const auto wanted = expected.find(of.segment_id);
    if (wanted != expected.end() && wanted->second == block && !existing.contains(of.segment_id)) {
      existing.emplace(of.segment_id, Panel {entity, block});
    } else {
      stale.push_back({entity, block});
    }
  });
  for (const Panel& panel : stale) {
    z13::station::DestroyBlock(panel.entity, panel.block);
  }
  for (const auto& [segment_id, block] : expected) {
    if (!existing.contains(segment_id)) {
      z13::station::SpawnBlock(world.entity(NextBlockName(world, counters).c_str()), block)
          .set(WallPanelOf {.segment_id = segment_id});
    }
  }
}

void RegisterSystems(flecs::world world) {
  const auto nodes = world.query<const WallNode>("WallPanelSystem::Nodes");
  const auto segments = world.query<const WallSegment>("WallPanelSystem::Segments");
  const auto panels = world.query<const WallPanelOf, const Block>("WallPanelSystem::Panels");
  world.system<z13::gameplay::IdCounters, const BlockPalette>("WallPanelSystem::SyncPanels")
      .kind<StationBuildPhase>()
      .with<StationMode>()
      .read<WallNode>()
      .read<WallSegment>()
      .read<WallPanelOf>()
      .write<Block>()
      .write<WallPanelOf>()
      .write<z13::station::BlockChange>()
      .each([nodes, segments, panels](
                flecs::iter& it, size_t, z13::gameplay::IdCounters& counters, const BlockPalette& palette) {
        SyncPanels(it.world(), nodes, segments, panels, counters, palette);
      });
}

}  // namespace

void WallPanelSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
