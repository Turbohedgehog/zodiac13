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

#include <cstddef>
#include <cstdint>

#include <Eigen/Dense>

#include <primitives/placement.h>

namespace z13::station::light {

// Light floods between nodes, each the connected free cells of a meter cube (kNodeCells cells
// a side): a wall through a cube makes a node on each side of it.
inline constexpr int kNodeCells = 4;
// Pieces a cube keeps apart; more are joined into the last, which may let light through.
inline constexpr int kPiecesPerNode = 3;
// The label of a cell light doesn't pass; free cells are labelled with their piece.
inline constexpr uint8_t kOpaqueLabel = 0xFF;

// What a node texel holds: its light, or the direction toward its lamps (CellLight::DirectionAt).
enum class NodeTexel : uint8_t { kLight, kDirection };

// Where the light's cubes lie: `cubes` of them from `origin`, a multiple of kNodeCells.
struct LightLayout {
  Eigen::Vector3i origin = Eigen::Vector3i::Zero();
  Eigen::Vector3i cubes = Eigen::Vector3i::Zero();

  Eigen::Vector3i Cells() const { return cubes * kNodeCells; }
  z13::building::primitives::CellBox CellBounds() const { return {.min = origin, .extent = Cells()}; }
  size_t CubeCount() const { return static_cast<size_t>(cubes.cast<int64_t>().prod()); }
  size_t CellCount() const { return static_cast<size_t>(Cells().cast<int64_t>().prod()); }
  // Of a cube, in cubes from the origin; cells are numbered alike along Cells().
  size_t CubeIndex(const Eigen::Vector3i& cube) const;
  size_t CellIndex(const Eigen::Vector3i& cell) const;
  // A node is one piece of a cube: kPiecesPerNode to a cube, in cube order.
  static size_t NodeIndex(size_t cube, uint8_t piece) { return (cube * kPiecesPerNode) + piece; }

  bool operator==(const LightLayout&) const = default;
};

}  // namespace z13::station::light
