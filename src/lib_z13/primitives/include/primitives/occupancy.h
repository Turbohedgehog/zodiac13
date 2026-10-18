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

#include <optional>
#include <span>
#include <vector>

#include <Eigen/Dense>

#include <primitives/geometry.h>
#include <primitives/placement.h>
#include <z13/components/block.h>

// Which cells a block fills: those its convex solids reach with some volume, so the empty
// half of a wedge stays free for another block.
namespace z13::building::primitives {

struct CellShape {
  CellBox box;
  // Per cell of `box`, x fastest; absent when the block fills the whole box.
  std::optional<std::vector<bool>> filled;

  static CellShape Full(const CellBox& box) { return {.box = box, .filled = std::nullopt}; }

  bool Contains(const Eigen::Vector3i& cell) const;
  bool Overlaps(const CellBox& cells) const;
  bool Overlaps(const CellShape& other) const;

  bool operator==(const CellShape&) const = default;
};

// The cells of a primitive of `size`, in its own frame, that `solids` reach with volume.
std::vector<bool> FilledCells(std::span<const ConvexSolid> solids, const Eigen::Vector3i& size);

// The block's cells; boxes and door frames fill their whole box without building solids.
CellShape ShapeOfBlock(const z13::station::Block& block, OptionalPalette palette);

// The boxes air cannot enter: the cells whose centre lies in the block's solids, so a slope
// holds back the air under its surface and no more. The same blocks always give the same
// boxes, which room edits rely on.
std::vector<CellBox> SealedBoxes(const z13::station::Block& block, OptionalPalette palette);

}  // namespace z13::building::primitives
