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

// A wall's stick as one of its ends carries it: the edge a bridge runs from or to.
struct WallStick {
  z13::station::WallAxis axis = z13::station::WallAxis::kZ;
  int length {};

  bool operator==(const WallStick&) const = default;
};

// The sticks of the walls standing at the node, in segment id order.
std::vector<WallStick> SticksAt(flecs::world world, uint32_t node_id);

// The wall bridging the stick at `first` to the same stick at `second`, thickened to the side
// nearer `eye`, or why there is none. The first node's stick is that of its oldest wall.
std::expected<FacedWall, std::string> PlanBridge(
    flecs::world world, uint32_t first, uint32_t second, int thickness, const Eigen::Vector3f& eye,
    const z13::building::primitives::Palette& palette);

}  // namespace z13::building
