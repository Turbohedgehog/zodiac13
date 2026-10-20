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

#include "wall_joints.h"

#include <algorithm>
#include <vector>

namespace z13::building {

namespace {

using z13::building::primitives::PlanSeam;
using z13::building::primitives::PlanWallAt;

// One wall's end at a node.
struct WallEndAt {
  const JointWall* wall {};
  bool at_a {};
};

void SetSeam(WallJoints& joints, const WallEndAt& end, const z13::building::primitives::WallSeam& seam) {
  auto& ends = joints.ends[end.wall->id];
  (end.at_a ? ends.at_a : ends.at_b) = seam;
}

void SolveNode(WallJoints& joints, std::vector<WallEndAt> meeting, int max_reach) {
  if (meeting.size() != 2 || meeting[0].wall == meeting[1].wall ||
      meeting[0].wall->geometry.stick_axis != meeting[1].wall->geometry.stick_axis) {
    return;
  }
  std::ranges::sort(meeting, {}, [](const WallEndAt& end) { return end.wall->id; });
  const auto first = PlanWallAt(meeting[0].wall->geometry, meeting[0].at_a);
  const auto second = PlanWallAt(meeting[1].wall->geometry, meeting[1].at_a);
  if (!first || !second) {
    return;
  }
  const auto seam = PlanSeam(*first, *second, max_reach);
  for (const WallEndAt& end : meeting) {
    if (seam) {
      SetSeam(joints, end, *seam);
    } else {
      joints.refused.emplace(end.wall->id, seam.error());
    }
  }
}

}  // namespace

WallJoints SolveJoints(std::span<const JointWall> walls, int max_reach) {
  std::map<uint32_t, std::vector<WallEndAt>> at_node;
  for (const JointWall& wall : walls) {
    at_node[wall.node_a].push_back({.wall = &wall, .at_a = true});
    at_node[wall.node_b].push_back({.wall = &wall, .at_a = false});
  }
  WallJoints joints;
  for (auto& [node, meeting] : at_node) {
    SolveNode(joints, std::move(meeting), max_reach);
  }
  return joints;
}

std::optional<std::string> JointProblem(
    std::span<const JointWall> before, std::span<const JointWall> after, int max_reach) {
  const WallJoints known = SolveJoints(before, max_reach);
  const WallJoints made = SolveJoints(after, max_reach);
  for (const auto& [wall, reason] : made.refused) {
    if (!known.refused.contains(wall)) {
      return reason;
    }
  }
  return std::nullopt;
}

}  // namespace z13::building
