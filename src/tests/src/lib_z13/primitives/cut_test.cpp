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

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <Eigen/Dense>

#include <z13_primitives/cut.h>

namespace z13::building::primitives {
namespace {

using z13::station::Block;
using z13::station::Orientation;

Primitive Stretchable() {
  return {.id = 2, .min_size = {1, 1, 1}, .max_size = {256, 1, 256}};
}

Primitive Fixed() {
  return {.id = 5, .min_size = {4, 4, 4}, .max_size = {4, 4, 4}};
}

int CellCount(const std::vector<Block>& blocks) {
  int cells = 0;
  for (const Block& block : blocks) {
    cells += OccupiedCells(block).extent.prod();
  }
  return cells;
}

bool Disjoint(const std::vector<Block>& blocks) {
  for (size_t i = 0; i < blocks.size(); ++i) {
    for (size_t j = i + 1; j < blocks.size(); ++j) {
      if (OccupiedCells(blocks[i]).Overlaps(OccupiedCells(blocks[j]))) {
        return false;
      }
    }
  }
  return true;
}

// A 20-cell wall, 1 thick and 12 tall, standing in the XZ plane.
const Block kWall {.spec = {.type_id = 2, .size = {20, 1, 12}}, .cell = {0, 0, 0}};

TEST(CutTest, ADoorwayLeavesTheWallAroundIt) {
  const std::vector<Block> left = LeftAfterCut(kWall, Stretchable(), {.min = {8, 0, 0}, .extent = {4, 1, 10}});

  // Left and right of the opening, and above it.
  ASSERT_EQ(left.size(), 3u);
  EXPECT_EQ(CellCount(left), 20 * 12 - 4 * 10);
  EXPECT_TRUE(Disjoint(left));
  EXPECT_EQ(OccupiedCells(left[0]).min, Eigen::Vector3i(0, 0, 0));
  EXPECT_EQ(OccupiedCells(left[0]).extent, Eigen::Vector3i(8, 1, 12));
  EXPECT_EQ(left[0].spec.size, Eigen::Vector3i(8, 1, 12));
  EXPECT_EQ(OccupiedCells(left[1]).min, Eigen::Vector3i(12, 0, 0));
  EXPECT_EQ(OccupiedCells(left[2]).min, Eigen::Vector3i(8, 0, 10));
}

TEST(CutTest, AWindowInTheMiddleLeavesFourBlocks) {
  const std::vector<Block> left = LeftAfterCut(kWall, Stretchable(), {.min = {8, 0, 4}, .extent = {4, 1, 4}});

  ASSERT_EQ(left.size(), 4u);
  EXPECT_EQ(CellCount(left), 20 * 12 - 4 * 4);
  EXPECT_TRUE(Disjoint(left));
}

TEST(CutTest, ACutBeyondTheBlockRemovesOnlyWhatOverlaps) {
  const std::vector<Block> left = LeftAfterCut(kWall, Stretchable(), {.min = {-5, -3, 0}, .extent = {10, 9, 12}});

  ASSERT_EQ(left.size(), 1u);
  EXPECT_EQ(OccupiedCells(left[0]).min, Eigen::Vector3i(5, 0, 0));
  EXPECT_EQ(left[0].spec.size, Eigen::Vector3i(15, 1, 12));
}

TEST(CutTest, AWholeCoverLeavesNothingAndAMissLeavesTheBlock) {
  EXPECT_TRUE(LeftAfterCut(kWall, Stretchable(), {.min = {-1, -1, -1}, .extent = {30, 3, 30}}).empty());

  const std::vector<Block> untouched = LeftAfterCut(kWall, Stretchable(), {.min = {40, 0, 0}, .extent = {2, 1, 2}});
  ASSERT_EQ(untouched.size(), 1u);
  EXPECT_EQ(untouched[0].cell, kWall.cell);
  EXPECT_EQ(untouched[0].spec, kWall.spec);
}

TEST(CutTest, AFixedBlockIsRemovedWhole) {
  const Block wedge {.spec = {.type_id = 5, .size = {4, 4, 4}}, .cell = {0, 0, 0}};

  EXPECT_TRUE(LeftAfterCut(wedge, Fixed(), {.min = {3, 3, 3}, .extent = {1, 1, 1}}).empty());
}

TEST(CutTest, ATurnedWallSplitsAlongItsOwnAxes) {
  for (const Orientation orientation : kOrientations) {
    const Block wall {.spec = {.type_id = 2, .size = {20, 1, 12}, .orientation = orientation}, .cell = {-3, 4, 7}};
    const CellBox whole = OccupiedCells(wall);
    // Inside the wall along its two long sides, through the whole thin one.
    const Eigen::Vector3i shrink = (whole.extent.array() > 1).select(Eigen::Vector3i(2, 2, 2), Eigen::Vector3i::Zero());
    const CellBox cut {.min = whole.min + shrink / 2, .extent = whole.extent - shrink};

    const std::vector<Block> left = LeftAfterCut(wall, Stretchable(), cut);

    // A wall within the primitive's limits stays valid after any turn: the thin axis is never cut.
    EXPECT_EQ(CellCount(left), whole.extent.prod() - cut.extent.prod()) << static_cast<int>(orientation);
    EXPECT_TRUE(Disjoint(left));
    for (const Block& piece : left) {
      EXPECT_EQ(piece.spec.orientation, orientation);
      const CellBox cells = OccupiedCells(piece);
      EXPECT_FALSE(cells.Overlaps(cut));
      EXPECT_TRUE(cells.Intersection(whole).extent == cells.extent);
    }
  }
}

}  // namespace
}  // namespace z13::building::primitives
