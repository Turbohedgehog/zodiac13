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

#include "wall_bridge.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

#include <primitives/wall_bend.h>

#include "wall_merge.h"

namespace z13::building {

using z13::station::WallStick;

std::vector<WallStick> SticksAt(flecs::world world, uint32_t node_id) {
  std::vector<std::pair<uint32_t, WallStick>> found;
  world.query<const z13::station::WallSegment>().each([&](const z13::station::WallSegment& segment) {
    if (segment.node_a == node_id) {
      found.push_back({segment.id, segment.stick_a});
    } else if (segment.node_b == node_id) {
      found.push_back({segment.id, segment.stick_b});
    }
  });
  std::ranges::sort(found, {}, &std::pair<uint32_t, WallStick>::first);
  std::vector<WallStick> sticks;
  std::ranges::transform(found, std::back_inserter(sticks), &std::pair<uint32_t, WallStick>::second);
  return sticks;
}

std::expected<FacedWall, std::string> PlanBridge(
    flecs::world world, uint32_t first, uint32_t second, int thickness, const Eigen::Vector3f& eye,
    const z13::building::primitives::Palette& palette) {
  const auto first_cell = WallNodeCell(world, first);
  const auto second_cell = WallNodeCell(world, second);
  if (first == second || !first_cell || !second_cell) {
    return std::unexpected(std::string {"two different wall nodes are needed"});
  }
  const std::vector<WallStick> sticks = SticksAt(world, first);
  const std::vector<WallStick> reached = SticksAt(world, second);
  if (sticks.empty() || reached.empty()) {
    return std::unexpected(std::string {"both nodes need a wall to take a stick from"});
  }
  const WallStick& stick = sticks.front();
  const auto reaches = [&](const auto& wanted) { return std::ranges::find_if(reached, wanted); };
  auto far = reaches([&stick](const WallStick& other) {
    return other.axis == stick.axis && (other.length > 0) == (stick.length > 0);
  });
  if (far == reached.end()) {
    far = reaches([&stick](const WallStick& other) { return other.axis != stick.axis; });
  }
  if (far == reached.end()) {
    return std::unexpected(std::string {"the second node's sticks point opposite to the first's"});
  }
  z13::building::primitives::WallGeometry wall {
      .node_a = *first_cell, .node_b = *second_cell, .stick_a = stick, .stick_b = *far, .thickness = thickness};
  if (z13::building::primitives::IsBent(wall)) {
    wall.diagonal = z13::building::primitives::DefaultDiagonal(wall);
  }
  return FacingEye(wall, eye, palette);
}

}  // namespace z13::building
