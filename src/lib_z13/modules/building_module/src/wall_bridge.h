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
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <z13/components/wall.h>
#include <primitives/palette.h>

#include "wall_brush.h"

namespace z13::building {

// The sticks of the walls standing at the node, in segment id order.
std::vector<z13::station::WallStick> SticksAt(flecs::world world, uint32_t node_id);

// The sticks standing at the node as lines from its cell, for the brush to show; none for a
// node that isn't there.
std::vector<z13::station::BrushLine> StickLinesAt(flecs::world world, uint32_t node_id);

// The wall bridging the stick at `first` (its oldest wall's) to one at `second`, thickened to the
// side nearer `eye`, or why there is none. The one at `second` is the oldest of the same axis and
// direction (a trapezoid when the lengths differ), else the oldest of another axis (a bent wall,
// along the diagonal that folds it least, or the other with `flip_diagonal`).
std::expected<FacedWall, std::string> PlanBridge(
    flecs::world world, uint32_t first, uint32_t second, int thickness, const Eigen::Vector3f& eye,
    const z13::building::primitives::Palette& palette, bool flip_diagonal = false);

}  // namespace z13::building
