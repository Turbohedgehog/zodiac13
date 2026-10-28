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
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <lib_core/state/world_serializer.h>
#include <lib_core/state/state_markers.h>
#include <lib_core/time/simulation_clock.h>

#include <sectors/sector.h>
#include <z13/components/block.h>
#include <z13/components/state_changes.h>
#include <z13/components/wall.h>
#include <z13_settings/net_tuning.h>

#include "interest_events.h"
#include "net_session.h"
#include "session_deltas.h"
#include "wire_records.h"

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

// The cells an entity stands in: its block's, a wall's nodes'. None for state that has no place, which every
// client that could see the player gets.
std::vector<Eigen::Vector3i> CellsOf(flecs::world world, flecs::entity entity, const NodeCells& nodes) {
  std::vector<Eigen::Vector3i> cells;
  if (const auto* block = entity.try_get<z13::station::Block>()) {
    cells.push_back(block->cell);
  } else if (const auto* node = entity.try_get<z13::station::WallNode>()) {
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
  return cells;
}

struct Edit {
  StateChange change;
  std::vector<Eigen::Vector3i> cells;
  std::optional<ft::EntitySnapshot> state;  // a set entity's, at the end of the tick
};

std::vector<Edit> EditsOfTick(flecs::world world) {
  std::vector<Edit> edits;
  const NodeCells nodes = CellsOfNodes(world);
  world.query<const StateChange>().each([&](const StateChange& change) {
    Edit edit {.change = change};
    if (change.kind == StateChangeKind::kRemoved) {
      if (change.cell) {
        edit.cells.push_back(*change.cell);
      }
    } else {
      const flecs::entity entity = world.entity(change.entity);
      if (!entity.is_alive() || !entity.has<ft::StateEntity>()) {
        return;  // set and then removed within the tick
      }
      edit.change.name = entity.name().c_str();
      edit.cells = CellsOf(world, entity, nodes);
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

bool Concerns(const Edit& edit, const ClientKnowledge& known, int sector_cells) {
  return edit.cells.empty() || std::ranges::any_of(edit.cells, [&](const Eigen::Vector3i& cell) {
    return known.sectors.contains(sectors::SectorOf(cell, sector_cells));
  });
}

}  // namespace

void SendWorldEdits(flecs::world world) {
  const std::vector<Edit> edits = EditsOfTick(world);
  if (edits.empty()) {
    return;
  }
  auto& session = world.get_mut<NetSession>();
  const auto& knowledge = world.get<InterestKnowledge>();
  const int sector_cells = world.get<NetTuning>().sector_size_cells;
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
          !Concerns(edit, known->second, sector_cells)) {
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
