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

#include "blueprint_walls.h"

#include <algorithm>
#include <format>
#include <map>

#include "wall_entities.h"
#include "wall_joints.h"

namespace z13::building {

namespace {

using z13::building::primitives::WallGeometry;

struct CellLess {
  bool operator()(const Eigen::Vector3i& a, const Eigen::Vector3i& b) const {
    return std::lexicographical_compare(a.data(), a.data() + 3, b.data(), b.data() + 3);
  }
};

std::vector<JointWall> JointWallsOfBlueprint(
    std::span<const WallGeometry> walls, std::span<const std::vector<z13::station::WallOpening>> openings = {}) {
  std::map<Eigen::Vector3i, uint32_t, CellLess> node_ids;
  const auto node_of = [&node_ids](const Eigen::Vector3i& cell) {
    return node_ids.emplace(cell, static_cast<uint32_t>(node_ids.size() + 1)).first->second;
  };
  std::vector<JointWall> joint_walls;
  joint_walls.reserve(walls.size());
  for (const WallGeometry& wall : walls) {
    joint_walls.push_back({.id = static_cast<uint32_t>(joint_walls.size() + 1),
                           .node_a = node_of(wall.node_a),
                           .node_b = node_of(wall.node_b),
                           .geometry = wall,
                           .openings = openings.empty() ? std::vector<z13::station::WallOpening> {}
                                                        : openings[joint_walls.size()]});
  }
  return joint_walls;
}

}  // namespace

std::expected<std::vector<z13::station::Block>, std::string> BlueprintPanels(
    std::span<const WallGeometry> walls, const z13::building::primitives::Palette& palette,
    const BuildingTuning& tuning, std::span<const std::vector<z13::station::WallOpening>> openings) {
  const std::vector<JointWall> joint_walls = JointWallsOfBlueprint(walls, openings);
  const WallJoints joints = SolveJoints(joint_walls, tuning.wall_joint_max_reach_cells);
  std::vector<z13::station::Block> panels;
  for (const JointWall& wall : joint_walls) {
    const auto blocks = JointPanels(wall, joints, palette);
    if (!blocks) {
      return std::unexpected(std::format("wall {}: {}", wall.id - 1, blocks.error()));
    }
    panels.insert(panels.end(), blocks->begin(), blocks->end());
  }
  return panels;
}

std::vector<std::string> BlueprintJointRefusals(std::span<const WallGeometry> walls, const BuildingTuning& tuning) {
  const WallJoints joints = SolveJoints(JointWallsOfBlueprint(walls), tuning.wall_joint_max_reach_cells);
  std::vector<std::string> refusals;
  for (const auto& [wall, why] : joints.refused) {
    refusals.push_back(std::format("wall {}: {}", wall - 1, why));
  }
  return refusals;
}

void SpawnBlueprintWalls(
    flecs::world world, z13::gameplay::SpawnNamer& namer, std::span<const WallGeometry> walls,
    std::span<const std::vector<z13::station::WallOpening>> openings) {
  for (size_t i = 0; i < walls.size(); ++i) {
    const z13::station::WallId segment_id = SpawnWall(world, namer, walls[i]).get<z13::station::WallSegment>().id;
    if (openings.empty()) {
      continue;
    }
    for (z13::station::WallOpening opening : openings[i]) {
      opening.segment_id = segment_id;
      SpawnWallOpening(world, namer, opening);
    }
  }
}

}  // namespace z13::building
