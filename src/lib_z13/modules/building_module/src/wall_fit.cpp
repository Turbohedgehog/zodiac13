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

#include "wall_fit.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include <z13/components/block.h>
#include <z13/components/wall.h>
#include <primitives/wall_bend.h>

#include "wall_entities.h"
#include "wall_joints.h"

namespace z13::building {

namespace {

using z13::building::primitives::Flipped;
using z13::building::primitives::FoldDegrees;
using z13::building::primitives::FoldWithin;

bool SharesNode(const JointWall& wall, const JointWall& other) {
  return wall.node_a == other.node_a || wall.node_a == other.node_b || wall.node_b == other.node_a ||
         wall.node_b == other.node_b;
}

// The panels standing for the walls, which a re-cut replaces.
std::vector<grid::BlockId> PanelsOf(flecs::world world, const std::vector<JointWall>& walls) {
  std::vector<grid::BlockId> panels;
  world.query<const z13::station::WallPanelOf>().each(
      [&](flecs::entity panel, const z13::station::WallPanelOf& of) {
        if (std::ranges::contains(walls, of.segment_id, &JointWall::id)) {
          panels.push_back(panel.id());
        }
      });
  return panels;
}

}  // namespace

Status CheckWallFits(flecs::world world, const z13::building::primitives::WallGeometry& wall, const BuildView& view) {
  const int max_fold = view.tuning.get().wall_fold_max_degrees;
  if (!FoldWithin(wall, max_fold)) {
    z13::building::primitives::WallGeometry other = wall;
    other.diagonal = Flipped(wall.diagonal);
    return std::unexpected(std::format(
        "the wall folds {:.0f} degrees, more than the {} allowed{}", FoldDegrees(wall), max_fold,
        FoldWithin(other, max_fold) ? "; the other diagonal would fold it less" : ""));
  }
  const int reach = view.tuning.get().wall_joint_max_reach_cells;
  const std::vector<JointWall> after = JointWallsWith(world, wall);
  if (const auto problem = JointProblem(JointWallsOf(world), after, reach)) {
    return std::unexpected(std::format("a joint would break: {}", *problem));
  }
  const JointWall& added = after.back();
  std::vector<JointWall> changed;
  std::ranges::copy_if(after, std::back_inserter(changed), [&](const JointWall& other) {
    return SharesNode(added, other);
  });
  const std::vector<grid::BlockId> replaced = PanelsOf(world, changed);
  const WallJoints joints = SolveJoints(after, reach);
  for (const JointWall& panel_wall : changed) {
    const auto panels = JointPanels(panel_wall, joints, view.palette);
    if (!panels) {
      return std::unexpected(panels.error());
    }
    for (const z13::station::Block& panel : *panels) {
      if (const Status fits = ValidateBuild(panel, view, z13::station::BrushPreview::Kind::kBuild, replaced); !fits) {
        return fits;
      }
    }
  }
  return {};
}

Status CheckOpeningFits(flecs::world world, const z13::station::WallOpening& opening, const BuildView& view) {
  const std::vector<JointWall> walls = JointWallsOf(world);
  const auto found = std::ranges::find(walls, opening.segment_id, &JointWall::id);
  if (found == walls.end()) {
    return std::unexpected(std::string {"the opening's wall is gone"});
  }
  JointWall wall = *found;
  wall.openings.push_back(opening);
  const WallJoints joints = SolveJoints(walls, view.tuning.get().wall_joint_max_reach_cells);
  const auto panels = JointPanels(wall, joints, view.palette);
  if (!panels) {
    return std::unexpected(panels.error());
  }
  const std::vector<grid::BlockId> replaced = PanelsOf(world, {wall});
  for (const z13::station::Block& panel : *panels) {
    if (const Status fits = ValidateBuild(panel, view, z13::station::BrushPreview::Kind::kBuild, replaced); !fits) {
      return fits;
    }
  }
  return {};
}

}  // namespace z13::building
