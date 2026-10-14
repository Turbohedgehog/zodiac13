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

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <primitives/placement.h>

#include <cell_light/light_layout.h>

namespace z13::station::light {

// The six ways out of a cube: +X, -X, +Y, -Y, +Z, -Z.
inline constexpr size_t kCubeSides = 6;

// The nodes of a layout: each cell's label (light_layout.h), and per node its centre and the
// pieces of the next cubes it touches, bit side * kPiecesPerNode + piece of Links().
// What a cube holds: one piece of free cells, none, or anything else.
enum class CubeKind : uint8_t { kFree, kOpaque, kMixed };

class LightNodes {
 public:
  LightNodes() = default;
  // Every cell free and unlabelled until Relabel.
  explicit LightNodes(const LightLayout& layout);

  const LightLayout& Layout() const { return layout_; }
  const std::vector<uint8_t>& Labels() const { return labels_; }
  // The opaque cells of `other` that lie inside this layout.
  void CopyOpaque(const LightNodes& other);
  // The part of `cells` inside the layout.
  void SetOpaque(const z13::building::primitives::CellBox& cells, bool opaque);
  // Finds the pieces of `cubes` again, and the links of those and of the cubes around them.
  void Relabel(const z13::building::primitives::CellBox& cubes);

  std::optional<size_t> NodeAt(const Eigen::Vector3i& cell) const;
  bool InCubes(size_t node, const z13::building::primitives::CellBox& cubes) const;
  // In cubes from the layout's origin.
  std::array<int, 3> CubeCoordinates(size_t node) const;
  // The cube next to one on that side; the caller knows it is inside the layout.
  size_t NeighbourCube(size_t cube, size_t side) const {
    const int64_t stride = cube_strides_[side / 2];
    return static_cast<size_t>(static_cast<int64_t>(cube) + (side % 2 == 0 ? stride : -stride));
  }
  // In cells, the mean of its cells' centres.
  const Eigen::Vector3f& Centre(size_t node) const { return centres_[node]; }
  uint32_t Links(size_t node) const { return links_[node]; }
  // Cubes with more pieces than kPiecesPerNode, whose last pieces share a node.
  size_t JoinedCubes() const;

 private:
  int64_t CubeIndex(const std::array<int, 3>& cube) const;
  // The layout's index of a cube's lowest cell, and the offset from it of a cell of the cube
  // (0..63, x first).
  int64_t CornerCell(const std::array<int, 3>& cube) const;
  int64_t CubeCellOffset(int cell) const { return cube_cell_offsets_[static_cast<size_t>(cell)]; }
  void LabelCube(const std::array<int, 3>& cube);
  void LinkCube(const std::array<int, 3>& cube);

  LightLayout layout_;
  std::vector<uint8_t> labels_;
  // Per cube, how many pieces it has, joined ones included, and its kind.
  std::vector<uint8_t> pieces_;
  std::vector<CubeKind> kinds_;
  std::vector<Eigen::Vector3f> centres_;
  std::vector<uint32_t> links_;
  // Index steps along x, y and z, between cells and between cubes.
  std::array<int64_t, 3> cell_strides_ {};
  std::array<int64_t, 3> cube_strides_ {};
  std::array<int64_t, kNodeCells * kNodeCells * kNodeCells> cube_cell_offsets_ {};
};

}  // namespace z13::station::light
