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

#include "wall_entities.h"

#include <algorithm>
#include <format>
#include <map>
#include <numeric>
#include <string>
#include <utility>

#include <lib_core/state/state_markers.h>

namespace z13::building {

namespace {

using z13::station::WallNode;
using z13::station::WallSegment;

std::optional<flecs::entity> Find(flecs::world world, const std::string& name) {
  const flecs::entity entity = world.lookup(name.c_str());
  return entity ? std::optional(entity) : std::nullopt;
}

std::string NodeName(uint32_t id) {
  return std::format("WallNode_{}", id);
}

std::string SegmentName(uint32_t id) {
  return std::format("WallSegment_{}", id);
}

std::string OpeningName(uint32_t id) {
  return std::format("WallOpening_{}", id);
}

bool UsesNode(flecs::world world, uint32_t node_id, uint32_t except_segment) {
  bool used = false;
  world.query<const WallSegment>().each([&](const WallSegment& segment) {
    used = used || (segment.id != except_segment && (segment.node_a == node_id || segment.node_b == node_id));
  });
  return used;
}

}  // namespace

flecs::entity SpawnWallNode(flecs::world world, z13::gameplay::IdCounters& counters, const Eigen::Vector3i& cell) {
  const uint32_t id = ++counters.last_wall_node_id;
  return world.entity(NodeName(id).c_str())
      .add<z13::flecs_tools::StateEntity>()
      .set(WallNode {.id = id, .cell = cell});
}

flecs::entity SpawnWallSegment(
    flecs::world world, z13::gameplay::IdCounters& counters, const WallSegment& segment) {
  WallSegment made = segment;
  made.id = ++counters.last_wall_segment_id;
  return world.entity(SegmentName(made.id).c_str()).add<z13::flecs_tools::StateEntity>().set(made);
}

uint32_t WallNodeAt(flecs::world world, z13::gameplay::IdCounters& counters, const Eigen::Vector3i& cell) {
  std::optional<uint32_t> found;
  world.query<const WallNode>().each([&](const WallNode& node) {
    if (!found && node.cell == cell) {
      found = node.id;
    }
  });
  if (found) {
    return *found;
  }
  SpawnWallNode(world, counters, cell);
  return counters.last_wall_node_id;
}

flecs::entity SpawnWall(
    flecs::world world, z13::gameplay::IdCounters& counters, const z13::building::primitives::WallGeometry& wall) {
  const uint32_t node_a = WallNodeAt(world, counters, wall.node_a);
  const uint32_t node_b = WallNodeAt(world, counters, wall.node_b);
  return SpawnWallSegment(world, counters,
                          WallSegment {.node_a = node_a, .node_b = node_b, .stick_a = wall.stick_a,
                                       .stick_b = wall.stick_b, .thickness = wall.thickness,
                                       .diagonal = wall.diagonal});
}

JointWall JointWallOf(
    const WallSegment& segment, const Eigen::Vector3i& cell_a, const Eigen::Vector3i& cell_b,
    std::vector<z13::station::WallOpening> openings) {
  return {.id = segment.id,
          .node_a = segment.node_a,
          .node_b = segment.node_b,
          .geometry = {.node_a = cell_a,
                       .node_b = cell_b,
                       .stick_a = segment.stick_a,
                       .stick_b = segment.stick_b,
                       .thickness = segment.thickness,
                       .diagonal = segment.diagonal},
          .openings = std::move(openings)};
}

flecs::entity SpawnWallOpening(
    flecs::world world, z13::gameplay::IdCounters& counters, const z13::station::WallOpening& opening) {
  z13::station::WallOpening made = opening;
  made.id = ++counters.last_wall_opening_id;
  return world.entity(OpeningName(made.id).c_str()).add<z13::flecs_tools::StateEntity>().set(made);
}

std::vector<z13::station::WallOpening> OpeningsOf(flecs::world world, uint32_t segment_id) {
  std::vector<z13::station::WallOpening> openings;
  world.query<const z13::station::WallOpening>().each([&](const z13::station::WallOpening& opening) {
    if (opening.segment_id == segment_id) {
      openings.push_back(opening);
    }
  });
  std::ranges::sort(openings, {}, &z13::station::WallOpening::id);
  return openings;
}

std::optional<flecs::entity> FindWallNode(flecs::world world, uint32_t id) {
  return Find(world, NodeName(id));
}

std::optional<flecs::entity> FindWallSegment(flecs::world world, uint32_t id) {
  return Find(world, SegmentName(id));
}

std::vector<JointWall> JointWallsOf(flecs::world world) {
  std::map<uint32_t, Eigen::Vector3i> cells;
  world.query<const WallNode>().each([&cells](const WallNode& node) { cells.emplace(node.id, node.cell); });
  std::map<uint32_t, std::vector<z13::station::WallOpening>> openings;
  world.query<const z13::station::WallOpening>().each(
      [&openings](const z13::station::WallOpening& opening) { openings[opening.segment_id].push_back(opening); });
  std::vector<JointWall> walls;
  world.query<const WallSegment>().each([&](const WallSegment& segment) {
    const auto a = cells.find(segment.node_a);
    const auto b = cells.find(segment.node_b);
    if (a != cells.end() && b != cells.end()) {
      walls.push_back(JointWallOf(segment, a->second, b->second, openings[segment.id]));
    }
  });
  std::ranges::sort(walls, {}, &JointWall::id);
  for (JointWall& wall : walls) {
    std::ranges::sort(wall.openings, {}, &z13::station::WallOpening::id);
  }
  return walls;
}

std::vector<JointWall> JointWallsWith(flecs::world world, const z13::building::primitives::WallGeometry& wall) {
  std::vector<JointWall> walls = JointWallsOf(world);
  using StandingNode = std::pair<Eigen::Vector3i, uint32_t>;
  std::vector<StandingNode> nodes;
  world.query<const WallNode>().each([&nodes](const WallNode& node) { nodes.emplace_back(node.cell, node.id); });
  uint32_t next_node = std::accumulate(nodes.begin(), nodes.end(), 0u, [](uint32_t highest, const StandingNode& node) {
    return std::max(highest, node.second);
  });
  const auto node_at = [&](const Eigen::Vector3i& cell) {
    const auto standing = std::ranges::find(nodes, cell, &StandingNode::first);
    if (standing != nodes.end()) {
      return standing->second;
    }
    nodes.emplace_back(cell, ++next_node);
    return next_node;
  };
  const uint32_t node_a = node_at(wall.node_a);
  const uint32_t node_b = node_at(wall.node_b);
  const uint32_t id = walls.empty() ? 1 : walls.back().id + 1;
  walls.push_back({.id = id, .node_a = node_a, .node_b = node_b, .geometry = wall});
  return walls;
}

void DestroyWallSegment(flecs::world world, uint32_t segment_id) {
  const auto segment = FindWallSegment(world, segment_id);
  if (!segment) {
    return;
  }
  const WallSegment gone = segment->get<WallSegment>();
  std::vector<flecs::entity> openings;
  world.query<const z13::station::WallOpening>().each(
      [&](flecs::entity entity, const z13::station::WallOpening& opening) {
        if (opening.segment_id == segment_id) {
          openings.push_back(entity);
        }
      });
  for (const flecs::entity opening : openings) {
    opening.destruct();
  }
  segment->destruct();
  for (const uint32_t node_id : {gone.node_a, gone.node_b}) {
    if (const auto node = FindWallNode(world, node_id); node && !UsesNode(world, node_id, gone.id)) {
      node->destruct();
    }
  }
}

}  // namespace z13::building
