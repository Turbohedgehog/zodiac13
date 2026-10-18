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

#include <format>
#include <string>

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

std::optional<flecs::entity> FindWallNode(flecs::world world, uint32_t id) {
  return Find(world, NodeName(id));
}

std::optional<flecs::entity> FindWallSegment(flecs::world world, uint32_t id) {
  return Find(world, SegmentName(id));
}

void DestroyWallSegment(flecs::world world, uint32_t segment_id) {
  const auto segment = FindWallSegment(world, segment_id);
  if (!segment) {
    return;
  }
  const WallSegment gone = segment->get<WallSegment>();
  segment->destruct();
  for (const uint32_t node_id : {gone.node_a, gone.node_b}) {
    if (const auto node = FindWallNode(world, node_id); node && !UsesNode(world, node_id, gone.id)) {
      node->destruct();
    }
  }
}

}  // namespace z13::building
