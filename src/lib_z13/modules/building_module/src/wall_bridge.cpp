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

#include "wall_merge.h"

namespace z13::building {

std::vector<WallStick> SticksAt(flecs::world world, uint32_t node_id) {
  std::vector<std::pair<uint32_t, WallStick>> found;
  world.query<const z13::station::WallSegment>().each([&](const z13::station::WallSegment& segment) {
    if (segment.node_a == node_id || segment.node_b == node_id) {
      const int growth = segment.node_b == node_id ? segment.stick_growth : 0;
      found.push_back({segment.id, {.axis = segment.stick_axis, .length = segment.stick_length + growth}});
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
  if (sticks.empty()) {
    return std::unexpected(std::string {"the first node has no wall to take a stick from"});
  }
  const WallStick& stick = sticks.front();
  const std::vector<WallStick> reached = SticksAt(world, second);
  const auto far = std::ranges::find_if(reached, [&stick](const WallStick& other) {
    return other.axis == stick.axis && (other.length > 0) == (stick.length > 0);
  });
  if (far == reached.end()) {
    return std::unexpected(std::string {"the second node carries no stick of that axis and direction"});
  }
  return FacingEye(
      {.node_a = *first_cell, .node_b = *second_cell, .stick_axis = stick.axis, .stick_length = stick.length,
       .stick_growth = far->length - stick.length, .thickness = thickness},
      eye, palette);
}

}  // namespace z13::building
