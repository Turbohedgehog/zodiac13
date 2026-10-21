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

#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>

#include <z13/components/block.h>
#include <primitives/palette.h>
#include <primitives/wall_seam.h>

namespace z13::building {

// A wall segment with its nodes' cells.
struct JointWall {
  uint32_t id {};
  uint32_t node_a {};
  uint32_t node_b {};
  z13::building::primitives::WallGeometry geometry;
};

// The joints of a set of walls: at a node where exactly two walls with the same stick axis
// meet, both end along one seam. Other nodes, joints whose seam can't be had, and walls with
// unequal sticks leave the walls square-cut there.
struct WallJoints {
  std::map<uint32_t, z13::building::primitives::WallEnds> ends;
  // The joints refused, by the ids of the walls in them, and why.
  std::map<uint32_t, std::string> refused;
};

WallJoints SolveJoints(std::span<const JointWall> walls, int max_reach);

// The panel `wall` stands as, cut along its joints' seams or, when that can't be had, square.
std::expected<z13::station::Block, std::string> JointPanel(
    const JointWall& wall, const WallJoints& joints, const z13::building::primitives::Palette& palette);

// Why `after` makes a joint `before` didn't refuse, if it does: what a change would break.
std::optional<std::string> JointProblem(
    std::span<const JointWall> before, std::span<const JointWall> after, int max_reach);

}  // namespace z13::building
