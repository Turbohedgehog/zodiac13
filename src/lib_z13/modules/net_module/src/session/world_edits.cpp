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

#include "world_edits.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/state_markers.h>
#include <lib_core/time/simulation_clock.h>

#include <primitives/placement.h>
#include <sectors/sector.h>
#include <z13/components/station/block.h>
#include <z13/components/station/block_changes.h>
#include <z13/components/station/grid_pose.h>
#include <z13/components/station/state_changes.h>
#include <z13/components/station/wall.h>
#include <z13_settings/net_tuning.h>

#include "../sectors/block_sectors.h"
#include "../sectors/interest_events.h"
#include "net_session.h"
#include "session_deltas.h"
#include "../protocol/wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
using z13::gameplay::StateChange;
using z13::gameplay::StateChangeKind;
using z13::station::WallId;

using NodeCells = std::map<WallId, Eigen::Vector3i>;

NodeCells CellsOfNodes(flecs::world world) {
  NodeCells cells;
  world.query<const z13::station::WallNode>().each(
      [&cells](const z13::station::WallNode& node) { cells.emplace(node.id, node.cell); });
  return cells;
}

void AddNodeCells(const NodeCells& nodes, std::initializer_list<WallId> ids, std::vector<Eigen::Vector3i>& cells) {
  for (const WallId id : ids) {
    if (const auto node = nodes.find(id); node != nodes.end()) {
      cells.push_back(node->second);
    }
  }
}

using GridSectors = std::unordered_map<z13::station::GridId, std::vector<sectors::SectorKey>>;

void SortUnique(std::vector<sectors::SectorKey>& keys) {
  const auto before = [](const sectors::SectorKey& a, const sectors::SectorKey& b) {
    return std::ranges::lexicographical_compare(a, b);
  };
  std::ranges::sort(keys, before);
  keys.erase(std::ranges::unique(keys).begin(), keys.end());
}

// A district reaches a client as a whole, so an edit in it concerns every client holding any sector the district
// touches; `cache` keeps them for the tick.
std::vector<sectors::SectorKey> SectorsOfBlock(
    flecs::world world, const z13::station::Block& block, const z13::station::GridPoses& poses, GridSectors& cache,
    int sector_cells) {
  std::vector<sectors::SectorKey> touched = BlockSectors(block, poses, sector_cells);
  if (block.grid == z13::station::kMainGrid) {
    return touched;
  }
  const auto [district, added] = cache.try_emplace(block.grid);
  if (added) {
    world.query<const z13::station::Block>().each([&](const z13::station::Block& other) {
      if (other.grid == block.grid) {
        std::ranges::copy(BlockSectors(other, poses, sector_cells), std::back_inserter(district->second));
      }
    });
    SortUnique(district->second);
  }
  std::ranges::copy(district->second, std::back_inserter(touched));
  SortUnique(touched);
  return touched;
}

// The sectors an entity stands in: every one a block touches, those of a wall's nodes. None for state that has no
// place, which every client that could see the player gets.
std::vector<sectors::SectorKey> SectorsOf(
    flecs::world world, flecs::entity entity, const NodeCells& nodes, const z13::station::GridPoses& poses,
    GridSectors& districts, int sector_cells) {
  if (const auto* block = entity.try_get<z13::station::Block>()) {
    return SectorsOfBlock(world, *block, poses, districts, sector_cells);
  }
  std::vector<Eigen::Vector3i> cells;
  if (const auto* node = entity.try_get<z13::station::WallNode>()) {
    cells.push_back(node->cell);
  } else if (const auto* segment = entity.try_get<z13::station::WallSegment>()) {
    AddNodeCells(nodes, {segment->node_a, segment->node_b}, cells);
  } else if (const auto* opening = entity.try_get<z13::station::WallOpening>()) {
    world.query<const z13::station::WallSegment>().each([&](const z13::station::WallSegment& segment) {
      if (segment.id == opening->segment_id) {
        AddNodeCells(nodes, {segment.node_a, segment.node_b}, cells);
      }
    });
  }
  std::vector<sectors::SectorKey> touched;
  std::ranges::transform(cells, std::back_inserter(touched),
                         [sector_cells](const Eigen::Vector3i& cell) { return sectors::SectorOf(cell, sector_cells); });
  return touched;
}

// A removed entity is gone by now; a block's whole box is still in the tick's BlockChange.
std::unordered_map<flecs::entity_t, z13::station::Block> RemovedBlocks(flecs::world world) {
  std::unordered_map<flecs::entity_t, z13::station::Block> removed;
  world.query<const z13::station::BlockChange>().each([&removed](const z13::station::BlockChange& change) {
    if (change.kind == z13::station::BlockChangeKind::kRemoved) {
      removed.insert_or_assign(change.entity, change.block);
    }
  });
  return removed;
}

struct Edit {
  StateChange change;
  std::vector<sectors::SectorKey> sectors;
  std::optional<ft::EntitySnapshot> state;  // a set entity's, at the end of the tick
};

std::vector<Edit> EditsOfTick(flecs::world world, int sector_cells) {
  std::vector<Edit> edits;
  const NodeCells nodes = CellsOfNodes(world);
  const z13::station::GridPoses poses = z13::station::PosesOf(world.query<const z13::station::Grid>());
  GridSectors districts;
  const auto removed_blocks = RemovedBlocks(world);
  world.query<const StateChange>().each([&](const StateChange& change) {
    Edit edit {.change = change};
    if (change.kind == StateChangeKind::kRemoved) {
      if (const auto block = removed_blocks.find(change.entity); block != removed_blocks.end()) {
        edit.sectors = SectorsOfBlock(world, block->second, poses, districts, sector_cells);
      } else if (change.cell) {
        edit.sectors.push_back(sectors::SectorOf(*change.cell, sector_cells));
      }
    } else {
      const flecs::entity entity = world.entity(change.entity);
      if (!entity.is_alive() || !entity.has<ft::StateEntity>()) {
        return;  // set and then removed within the tick
      }
      edit.change.name = entity.name().c_str();
      edit.sectors = SectorsOf(world, entity, nodes, poses, districts, sector_cells);
      if (const auto state = ft::CaptureEntityState(world, entity)) {
        // A block comes in a group of its chunk.
        if (const auto all = ft::AllEntities(*state); !all.empty()) {
          edit.state = all.front().get();
        }
      }
    }
    edits.push_back(std::move(edit));
  });
  std::ranges::sort(edits, {}, [](const Edit& edit) { return std::pair(edit.change.actor, edit.change.name); });
  return edits;
}

bool Concerns(const Edit& edit, const ClientKnowledge& known) {
  return edit.sectors.empty() || std::ranges::any_of(edit.sectors, [&known](const sectors::SectorKey& sector) {
           return known.sectors.contains(sector);
         });
}

}  // namespace

void SendWorldEdits(flecs::world world) {
  const std::vector<Edit> edits = EditsOfTick(world, world.get<NetTuning>().sector_size_cells);
  if (edits.empty()) {
    return;
  }
  auto& session = world.get_mut<NetSession>();
  const auto& knowledge = world.get<InterestKnowledge>();
  const uint64_t tick = world.get<ft::SimulationClock>().tick;
  auto& history = world.get_mut<ScheduledSessionDeltas>().history;
  for (const auto& [connection, client] : session.PlayerConnections()) {
    const auto known = knowledge.by_client.find(client);
    if (known == knowledge.by_client.end()) {
      continue;
    }
    ft::WorldSnapshot state;
    fbs::net::WorldEditT message;
    message.apply_tick = tick;
    for (const Edit& edit : edits) {
      const uint32_t actor = edit.change.actor;
      if (actor == client || PlayersAt(known->second, tick).contains(actor) ||
          !Concerns(edit, known->second)) {
        continue;
      }
      if (edit.change.kind == StateChangeKind::kRemoved) {
        message.removed.push_back(edit.change.name);
      } else if (edit.state) {
        state.entities.push_back(*edit.state);
      }
    }
    if (state.entities.empty() && message.removed.empty()) {
      continue;
    }
    message.entities = ToWire(state);
    Envelope envelope;
    envelope.body.Set(fbs::net::WorldEditT(message));
    session.Send(connection, Channel::kReliable, envelope);
    const auto at = std::ranges::upper_bound(history, tick, {}, &ScheduledSessionDelta::apply_tick);
    history.insert(at, {.apply_tick = tick, .delta = std::move(message), .recipient = client});
  }
}

}  // namespace z13::net
