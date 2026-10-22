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
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <primitives/wall_bend.h>
#include <primitives/wall_panel.h>

namespace z13::building {

namespace {

using z13::building::primitives::PlanSeam;
using z13::building::primitives::PlanTee;
using z13::building::primitives::PlanWall;
using z13::building::primitives::PlanWallAt;

// Nodes of two to four walls are solved.
constexpr size_t kMinWallsAtNode = 2;
constexpr size_t kMaxWallsAtNode = 4;

// One wall's end at a node.
struct WallEndAt {
  const JointWall* wall {};
  bool at_a {};
};

void SetSeam(WallJoints& joints, const WallEndAt& end, const z13::building::primitives::WallSeam& seam) {
  auto& ends = joints.ends[end.wall->id];
  (end.at_a ? ends.at_a : ends.at_b) = seam;
}

// The two walls of a node that run on through it, the lowest ids first: opposite ways along one
// line, thickened alike.
std::optional<std::array<size_t, 2>> ThroughPair(const std::vector<PlanWall>& plans) {
  for (size_t first = 0; first < plans.size(); ++first) {
    for (size_t second = first + 1; second < plans.size(); ++second) {
      const PlanWall& a = plans[first];
      const PlanWall& b = plans[second];
      if (a.direction.x() * b.direction.y() - a.direction.y() * b.direction.x() == 0 &&
          a.direction.dot(b.direction) < 0 && a.offset == b.offset) {
        return std::array<size_t, 2> {first, second};
      }
    }
  }
  return std::nullopt;
}

void Refuse(WallJoints& joints, const WallEndAt& end, const std::string& reason) {
  joints.refused.emplace(end.wall->id, reason);
}

// Two walls meet along one seam. Three or four meet as a T or an X when two run on through the
// node: the others end on the faces of those, the lower ids running through.
void SolveNode(WallJoints& joints, std::vector<WallEndAt> meeting, int max_reach) {
  const auto parallel = [](const JointWall& wall) { return !z13::building::primitives::IsBent(wall.geometry); };
  const auto fits = [&](const WallEndAt& end) {
    return parallel(*end.wall) && end.wall->geometry.stick_a.axis == meeting.front().wall->geometry.stick_a.axis;
  };
  if (meeting.size() < kMinWallsAtNode || meeting.size() > kMaxWallsAtNode || !std::ranges::all_of(meeting, fits)) {
    return;
  }
  std::ranges::sort(meeting, {}, [](const WallEndAt& end) { return end.wall->id; });
  if (std::ranges::adjacent_find(meeting, {}, [](const WallEndAt& end) { return end.wall; }) != meeting.end()) {
    return;
  }
  std::vector<PlanWall> plans;
  for (const WallEndAt& end : meeting) {
    const auto plan = PlanWallAt(end.wall->geometry, end.at_a);
    if (!plan) {
      return;
    }
    plans.push_back(*plan);
  }
  const std::optional<std::array<size_t, 2>> through =
      meeting.size() == kMinWallsAtNode ? std::optional(std::array<size_t, 2> {0, 1}) : ThroughPair(plans);
  if (!through) {
    return;
  }
  const auto seam = PlanSeam(plans[(*through)[0]], plans[(*through)[1]], max_reach);
  for (const size_t index : *through) {
    if (seam) {
      SetSeam(joints, meeting[index], *seam);
    } else {
      Refuse(joints, meeting[index], seam.error());
    }
  }
  for (size_t index = 0; index < meeting.size(); ++index) {
    if (index == (*through)[0] || index == (*through)[1]) {
      continue;
    }
    const auto tee = PlanTee(plans[index], plans[(*through)[0]], max_reach);
    if (tee) {
      SetSeam(joints, meeting[index], *tee);
    } else {
      Refuse(joints, meeting[index], tee.error());
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

std::expected<z13::station::Block, std::string> JointPanel(
    const JointWall& wall, const WallJoints& joints, const z13::building::primitives::Palette& palette) {
  const auto ends = joints.ends.find(wall.id);
  if (ends != joints.ends.end()) {
    if (auto cut = z13::building::primitives::WallPanelBlock(wall.geometry, palette, ends->second)) {
      return cut;
    }
  }
  return z13::building::primitives::WallPanelBlock(wall.geometry, palette);
}

std::expected<std::vector<z13::station::Block>, std::string> JointPanels(
    const JointWall& wall, const WallJoints& joints, const z13::building::primitives::Palette& palette) {
  if (z13::building::primitives::IsBent(wall.geometry)) {
    return z13::building::primitives::WallPanelBlocks(wall.geometry, palette);
  }
  auto panel = JointPanel(wall, joints, palette);
  if (!panel) {
    return std::unexpected(std::move(panel.error()));
  }
  return std::vector<z13::station::Block> {*panel};
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
