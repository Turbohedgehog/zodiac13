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

#include "cell_borrow.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace z13::station::light {

namespace {

using z13::building::primitives::CellBox;

// What a lit node's cell adds to the distance of an unlit one's: any lit cell within reach wins.
constexpr int kUnlitPenalty = 100;

// Plain numbers: this runs over every opaque cell of the layout, where Eigen's temporaries
// cost many times more in Debug.
struct Scan {
  int x {};
  int y {};
  int z {};
};

uint8_t BorrowOf(const LightNodes& nodes, std::span<const Eigen::Vector3f> light, const Scan& cell) {
  const LightLayout& layout = nodes.Layout();
  const Eigen::Vector3i size = layout.Cells();
  const std::vector<uint8_t>& labels = nodes.Labels();
  std::optional<int> nearest;
  uint8_t borrow = kNoBorrow;
  for (int dz = 0; dz < kBorrowLayers; ++dz) {
    for (int dy = -kBorrowReach; dy <= kBorrowReach; ++dy) {
      for (int dx = -kBorrowReach; dx <= kBorrowReach; ++dx) {
        const Scan other {cell.x + dx, cell.y + dy, cell.z + dz};
        if (other.x < 0 || other.y < 0 || other.z < 0 || other.x >= size.x() || other.y >= size.y() ||
            other.z >= size.z()) {
          continue;
        }
        const size_t index = (((static_cast<size_t>(other.z) * size.y()) + other.y) * size.x()) + other.x;
        const uint8_t label = labels[index];
        if (label >= kPiecesPerNode) {
          continue;
        }
        const Eigen::Vector3i cube(other.x / kNodeCells, other.y / kNodeCells, other.z / kNodeCells);
        const size_t node = LightLayout::NodeIndex(layout.CubeIndex(cube), label);
        const int distance = (dx * dx) + (dy * dy) + (dz * dz) + (light[node].maxCoeff() > 0.f ? 0 : kUnlitPenalty);
        if (!nearest || distance < *nearest) {
          nearest = distance;
          borrow = static_cast<uint8_t>((((dz * kBorrowSpan) + dy + kBorrowReach) * kBorrowSpan) + dx + kBorrowReach);
        }
      }
    }
  }
  return borrow;
}

}  // namespace

std::vector<CellTexel> CellTexelsOf(const LightNodes& nodes, std::span<const Eigen::Vector3f> light,
                                    const CellBox& cells) {
  const LightLayout& layout = nodes.Layout();
  std::vector<CellTexel> texels;
  texels.reserve(static_cast<size_t>(cells.extent.cast<int64_t>().prod()));
  const Eigen::Vector3i end = cells.End();
  for (int z = cells.min.z(); z < end.z(); ++z) {
    for (int y = cells.min.y(); y < end.y(); ++y) {
      for (int x = cells.min.x(); x < end.x(); ++x) {
        const Eigen::Vector3i local = Eigen::Vector3i(x, y, z) - layout.origin;
        const uint8_t label = nodes.Labels()[layout.CellIndex({x, y, z})];
        texels.push_back({.label = label,
                          .borrow = label == kOpaqueLabel ? BorrowOf(nodes, light, {local.x(), local.y(), local.z()})
                                                          : kNoBorrow});
      }
    }
  }
  return texels;
}

}  // namespace z13::station::light
