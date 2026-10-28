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

#include "wall_merge.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <vector>

#include <z13/components/wall.h>
#include <primitives/wall_bend.h>
#include <primitives/wall_panel.h>

#include "wall_entities.h"
#include "wall_joints.h"

namespace z13::building {

namespace {

using z13::station::WallId;

using z13::station::WallAxis;
using z13::station::WallNode;
using z13::station::WallSegment;

using NodeCells = std::map<WallId, Eigen::Vector3i>;

NodeCells CellsOfNodes(flecs::world world) {
  NodeCells cells;
  world.query<const WallNode>().each([&cells](const WallNode& node) { cells.emplace(node.id, node.cell); });
  return cells;
}

std::vector<WallSegment> SegmentsOf(flecs::world world) {
  std::vector<WallSegment> segments;
  world.query<const WallSegment>().each([&segments](const WallSegment& segment) { segments.push_back(segment); });
  return segments;
}

bool Touches(const WallSegment& segment, WallId node_id) {
  return segment.node_a == node_id || segment.node_b == node_id;
}

Eigen::Vector3i MergedCell(const Eigen::Vector3i& first, const Eigen::Vector3i& second, MergeAt at) {
  switch (at) {
    case MergeAt::kFirst:
      return first;
    case MergeAt::kLast:
      return second;
    case MergeAt::kCenter:
      break;
  }
  return ((first + second).array().unaryExpr([](int sum) { return sum >> 1; })).matrix();
}

// Whether `cell` lies strictly inside the line from `segment`'s node a to its node b, across
// the plane the stick leaves, on the stick's own level.
bool OnLine(const WallSegment& segment, const Eigen::Vector3i& a, const Eigen::Vector3i& b, const Eigen::Vector3i& cell) {
  const int stick = static_cast<int>(segment.stick_a.axis);
  if (cell[stick] != a[stick]) {
    return false;
  }
  const int first = stick == 0 ? 1 : 0;
  const int second = stick == 2 ? 1 : 2;
  const Eigen::Vector2i along {b[first] - a[first], b[second] - a[second]};
  const Eigen::Vector2i from {cell[first] - a[first], cell[second] - a[second]};
  const int reach = along.dot(from);
  return along.x() * from.y() - along.y() * from.x() == 0 && reach > 0 && reach < along.dot(along);
}

}  // namespace

std::optional<Eigen::Vector3i> WallNodeCell(flecs::world world, WallId node_id) {
  const NodeCells cells = CellsOfNodes(world);
  const auto found = cells.find(node_id);
  return found != cells.end() ? std::optional(found->second) : std::nullopt;
}

std::optional<WallId> NearestWallNode(flecs::world world, const Aim& aim, float snap_cells) {
  const NodeCells cells = CellsOfNodes(world);
  const auto distance = [&aim](const NodeCells::value_type& node) {
    return (node.second.cast<float>() - aim.point).norm();
  };
  const auto nearest = std::ranges::min_element(cells, {}, distance);
  return nearest != cells.end() && distance(*nearest) <= snap_cells ? std::optional(nearest->first) : std::nullopt;
}

std::expected<NodeMerge, std::string> PlanNodeMerge(
    flecs::world world, WallId first, WallId second, MergeAt at,
    const z13::building::primitives::Palette& palette, int joint_reach) {
  NodeCells cells = CellsOfNodes(world);
  const std::set<WallId> with_openings = SegmentsWithOpenings(world);
  const auto first_cell = cells.find(first);
  const auto second_cell = cells.find(second);
  if (first == second || first_cell == cells.end() || second_cell == cells.end()) {
    return std::unexpected(std::string {"two different wall nodes are needed"});
  }
  const NodeMerge merge {
      .keep = first, .drop = second, .cell = MergedCell(first_cell->second, second_cell->second, at)};
  cells[merge.keep] = merge.cell;
  for (const WallSegment& segment : SegmentsOf(world)) {
    if (!Touches(segment, first) && !Touches(segment, second)) {
      const auto from = cells.find(segment.node_a);
      const auto to = cells.find(segment.node_b);
      if (from == cells.end() || to == cells.end() || !OnLine(segment, from->second, to->second, merge.cell)) {
        continue;
      }
      if (segment.stick_a != segment.stick_b) {
        return std::unexpected(std::string {"a wall with unequal sticks can't be divided"});
      }
      if (with_openings.contains(segment.id)) {
        return std::unexpected(std::string {"a wall with openings can't be divided"});
      }
      continue;
    }
    if (with_openings.contains(segment.id)) {
      return std::unexpected(std::string {"a wall with openings can't be moved by a merge"});
    }
    if (Touches(segment, first) && Touches(segment, second)) {
      return std::unexpected(std::string {"a wall joins the two nodes"});
    }
    const auto a = cells.find(segment.node_a == merge.drop ? merge.keep : segment.node_a);
    const auto b = cells.find(segment.node_b == merge.drop ? merge.keep : segment.node_b);
    if (a == cells.end() || b == cells.end()) {
      continue;
    }
    const auto panels =
        z13::building::primitives::WallPanelBlocks(JointWallOf(segment, a->second, b->second).geometry, palette);
    if (!panels) {
      return std::unexpected(std::format("a wall would be left without a panel: {}", panels.error()));
    }
  }
  const std::vector<JointWall> before = JointWallsOf(world);
  std::vector<JointWall> after = before;
  for (JointWall& wall : after) {
    if (wall.node_a == merge.drop || wall.node_a == merge.keep) {
      wall.node_a = merge.keep;
      wall.geometry.node_a = merge.cell;
    }
    if (wall.node_b == merge.drop || wall.node_b == merge.keep) {
      wall.node_b = merge.keep;
      wall.geometry.node_b = merge.cell;
    }
  }
  if (const auto problem = JointProblem(before, after, joint_reach)) {
    return std::unexpected(std::format("a joint would break: {}", *problem));
  }
  return merge;
}

void ApplyNodeMerge(flecs::world world, z13::gameplay::SpawnNamer& namer, const NodeMerge& merge) {
  const NodeCells cells = CellsOfNodes(world);
  std::vector<WallSegment> halves;
  world.query<WallSegment>().each([&](WallSegment& segment) {
    if (Touches(segment, merge.drop)) {
      (segment.node_a == merge.drop ? segment.node_a : segment.node_b) = merge.keep;
    } else if (const auto a = cells.find(segment.node_a), b = cells.find(segment.node_b);
               !Touches(segment, merge.keep) && a != cells.end() && b != cells.end() &&
               OnLine(segment, a->second, b->second, merge.cell)) {
      halves.push_back(segment);
      segment.node_b = merge.keep;
    }
  });
  // Segment ids are numbered as they are spawned, so the order they are spawned in must be the same everywhere.
  std::ranges::sort(halves, {}, &WallSegment::id);
  for (WallSegment& half : halves) {
    half.node_a = merge.keep;
    SpawnWallSegment(world, namer, half);
  }
  if (const auto node = FindWallNode(world, merge.keep)) {
    node->get_mut<WallNode>().cell = merge.cell;
  }
  if (const auto node = FindWallNode(world, merge.drop)) {
    node->destruct();
  }
}

}  // namespace z13::building
