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

#include <Eigen/Dense>

#include <z13/components/station.h>
#include <z13_primitives/placement.h>

namespace z13::testing {

// The ship scene's building cube, in meters.
inline constexpr float kCubeEdge = static_cast<float>(z13::station::kCubeEdgeCells) * z13::station::kCellSize;

inline z13::primitives::Placement PlacementOf(const z13::station::Block& block) {
  return {.cell = block.cell, .size = block.size, .orientation = block.orientation};
}

// A cube snapped around `center`, as the ship scene's building places it.
inline z13::station::Block CubeAt(const Eigen::Vector3f& center) {
  const auto placement = z13::primitives::PlaceCentredOn(
      center / z13::station::kCellSize, Eigen::Vector3i::Constant(z13::station::kCubeEdgeCells), 0);
  return {.type_id = z13::station::kCubePrimitiveId, .cell = placement.cell, .size = placement.size};
}

// The middle of the cells a block occupies, in meters.
inline Eigen::Vector3f BlockCenter(const z13::station::Block& block) {
  const z13::primitives::CellBox cells = PlacementOf(block).Occupied();
  return (cells.min + cells.End()).cast<float>() / 2.f * z13::station::kCellSize;
}

}  // namespace z13::testing
