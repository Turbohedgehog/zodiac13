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
#include <optional>
#include <string>

#include <Eigen/Core>

#include <primitives/wall_panel.h>

namespace z13::building::primitives {

// One end of a wall in the plane of its stick axis, in that plane's two axes (the lower axis
// first): the node, the vector to the far end, and the vector the wall thickens by (along the
// lesser axis of the plane, to the side of its sign).
struct PlanWall {
  Eigen::Vector2i node = Eigen::Vector2i::Zero();
  Eigen::Vector2i direction = Eigen::Vector2i::Zero();
  Eigen::Vector2i offset = Eigen::Vector2i::Zero();
};

// The wall's end at node a (or b), nullopt for a wall PanelBlock would refuse for its shape.
std::optional<PlanWall> PlanWallAt(const WallGeometry& wall, bool at_a);

// The point (x / w, y / w), w > 0, and the line a·x + b·y + c = 0, both relative to the node.
// Whole numbers throughout, so the seam is the same on every platform.
struct PlanPoint {
  int64_t x {};
  int64_t y {};
  int64_t w = 1;
};

struct SeamLine {
  int64_t a {};
  int64_t b {};
  int64_t c {};
  bool operator==(const SeamLine&) const = default;
};

// The line both walls of a joint end at: it runs through the crossing of the two faces that
// face each other (inner) and of the two that face away (outer). Each wall keeps its strip on
// the side of the line its far end is on; a wall may reach back past the node, or be cut short.
struct WallSeam {
  SeamLine line;
  PlanPoint inner;
  PlanPoint outer;
};

// Which side of the line the point is on: -1, 0 or 1.
int SideOf(const SeamLine& line, const PlanPoint& point);

// The seam of two walls from one node. An error says why the joint can't be had, and what
// would make it: a tip reaching further than `max_reach` cells from the node, or a wall too
// short to keep a cell beyond the seam.
std::expected<WallSeam, std::string> PlanSeam(const PlanWall& first, const PlanWall& second, int max_reach);

}  // namespace z13::building::primitives
