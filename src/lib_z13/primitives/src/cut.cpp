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

#include <primitives/cut.h>

#include <algorithm>

namespace z13::building::primitives {

namespace {

constexpr int kAxes = 3;

bool WithinLimits(const Eigen::Vector3i& size, const Primitive& primitive) {
  return (size.array() >= primitive.min_size.array()).all() && (size.array() <= primitive.max_size.array()).all();
}

}  // namespace

std::vector<z13::station::Block> LeftAfterCut(
    const z13::station::Block& block, const Primitive& primitive, const CellBox& cut) {
  // A permutation: own size = axes^T * world extent.
  const Eigen::Matrix3i axes = OrientationMatrix(block.spec.orientation).cwiseAbs();
  const CellBox whole = OccupiedCells(block);
  const CellBox removed = whole.Intersection(cut);
  if ((removed.extent.array() < 1).any()) {
    return {block};
  }

  std::vector<CellBox> boxes;
  CellBox rest = whole;
  for (int axis = 0; axis < kAxes; ++axis) {
    const int below = removed.min[axis] - rest.min[axis];
    const int above = rest.End()[axis] - removed.End()[axis];
    if (below > 0) {
      CellBox box = rest;
      box.extent[axis] = below;
      boxes.push_back(box);
    }
    if (above > 0) {
      CellBox box = rest;
      box.min[axis] = removed.End()[axis];
      box.extent[axis] = above;
      boxes.push_back(box);
    }
    rest.min[axis] = removed.min[axis];
    rest.extent[axis] = removed.extent[axis];
  }

  std::vector<z13::station::Block> left;
  for (const CellBox& box : boxes) {
    const Eigen::Vector3i size = axes.transpose() * box.extent;
    if (!WithinLimits(size, primitive)) {
      return {};
    }
    left.push_back({.spec = {.type_id = block.spec.type_id, .size = size, .orientation = block.spec.orientation},
                    .cell = box.min});
  }
  return left;
}

}  // namespace z13::building::primitives
