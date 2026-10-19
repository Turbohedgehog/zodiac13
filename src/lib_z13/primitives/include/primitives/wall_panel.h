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

#include <expected>
#include <optional>
#include <string>

#include <Eigen/Dense>

#include <z13/components/block.h>
#include <z13/components/wall.h>
#include <primitives/palette.h>

namespace z13::building::primitives {

// A wall segment's ends and sticks, in cells.
struct WallGeometry {
  Eigen::Vector3i node_a = Eigen::Vector3i::Zero();
  Eigen::Vector3i node_b = Eigen::Vector3i::Zero();
  z13::station::WallAxis stick_axis = z13::station::WallAxis::kZ;
  int stick_length {};
  int thickness {};
};

// The axes of a stick axis's plane (the lower first) and which carries the panel's run (the
// longer of `span`, the lower on a tie) and which its rise (the other, the thickening one).
struct PlanAxes {
  int first {};
  int second {};
  int major {};
  int minor {};
};

PlanAxes PlanAxesOf(z13::station::WallAxis stick_axis, const Eigen::Vector3i& span);

// The thinnest panel the wall can be and still hold air back (palette Panel's AllowsSize): one
// cell along the grid, two across it. Nothing for nodes that coincide in the plane.
std::optional<int> MinWallThickness(const WallGeometry& wall);

// The Panel block whose prism runs from node a to node b (b's offset along the stick axis
// dropped) over the stick, and thickens by |thickness| cells along the lesser axis of the
// plane, to the side of its sign. Its ends are cut by the grid, so a wall meets one flush.
// An error for nodes that coincide in the plane, a stick of no length, or a thickness the
// panel can't have (palette Panel's AllowsSize).
std::expected<z13::station::Block, std::string> WallPanelBlock(const WallGeometry& wall, const Palette& palette);

}  // namespace z13::building::primitives
