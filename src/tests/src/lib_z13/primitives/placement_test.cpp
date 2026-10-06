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
#include <cstdint>
#include <vector>

#include <Eigen/Dense>

#include <z13_primitives/placement.h>

namespace z13::building::primitives {
namespace {

using z13::station::Block;
using z13::station::Orientation;

TEST(PlacementTest, TheTwentyFourOrientationsAreDistinctRotations) {
  std::vector<Eigen::Matrix3i> seen;
  for (const Orientation orientation : kOrientations) {
    const Eigen::Matrix3i m = OrientationMatrix(orientation);
    EXPECT_EQ(m * m.transpose(), Eigen::Matrix3i::Identity());
    EXPECT_EQ(m.cast<float>().determinant(), 1.f);
    for (const Eigen::Matrix3i& other : seen) {
      EXPECT_NE(m, other) << static_cast<int>(orientation);
    }
    seen.push_back(m);
  }
  EXPECT_EQ(OrientationMatrix(Orientation {}), Eigen::Matrix3i::Identity());
}

TEST(PlacementTest, InverseOrientationUndoesTheRotation) {
  for (const Orientation orientation : kOrientations) {
    const Orientation inverse = InverseOrientation(orientation);
    EXPECT_EQ(OrientationMatrix(inverse) * OrientationMatrix(orientation), Eigen::Matrix3i::Identity());
    EXPECT_EQ(InverseOrientation(inverse), orientation);
  }
}

// The posed frame [0, size] must cover exactly the occupied cells, whatever the turn.
TEST(PlacementTest, PoseMapsTheFrameOntoTheOccupiedCells) {
  for (const Orientation orientation : kOrientations) {
    const Block block {.spec = {.size = {4, 1, 7}, .orientation = orientation}, .cell = {3, -2, 5}};
    const Eigen::Vector3i& size = block.spec.size;
    const CellPose pose = PoseOf(block);
    const CellBox occupied = OccupiedCells(block);
    Eigen::Vector3i low = Eigen::Vector3i::Constant(INT32_MAX);
    Eigen::Vector3i high = Eigen::Vector3i::Constant(INT32_MIN);
    for (int corner = 0; corner < 8; ++corner) {
      const Eigen::Vector3i local(
          (corner & 1) ? size.x() : 0, (corner & 2) ? size.y() : 0, (corner & 4) ? size.z() : 0);
      const Eigen::Vector3i mapped = pose.origin + pose.rotation * local;
      low = low.cwiseMin(mapped);
      high = high.cwiseMax(mapped);
    }
    EXPECT_EQ(low, occupied.min) << static_cast<int>(orientation);
    EXPECT_EQ(high, occupied.End()) << static_cast<int>(orientation);
  }
}

TEST(PlacementTest, PlaceCentredOnSnapsTheTurnedExtentAroundThePoint) {
  // A quarter turn about Z swaps X and Y, so 4x2x1 occupies 2x4x1.
  const Block block =
      PlaceCentredOn({10.2f, 0.9f, -0.4f}, {.size = {4, 2, 1}, .orientation = Orientation::kFacePosYUpPosZ});
  EXPECT_EQ(OccupiedCells(block).extent, Eigen::Vector3i(2, 4, 1));
  EXPECT_EQ(block.cell, Eigen::Vector3i(9, -1, -1));
}

TEST(PlacementTest, QuarterTurnsReachEveryOrientationAndFourMakeAFullTurn) {
  std::vector<Orientation> reached {Orientation {}};
  for (size_t i = 0; i < reached.size(); ++i) {
    for (const TurnAxis axis : {TurnAxis::kX, TurnAxis::kY, TurnAxis::kZ}) {
      Orientation turned = reached[i];
      for (int quarter = 0; quarter < 4; ++quarter) {
        turned = QuarterTurn(turned, axis);
        if (std::ranges::find(reached, turned) == reached.end()) {
          reached.push_back(turned);
        }
      }
      EXPECT_EQ(turned, reached[i]);
    }
  }
  EXPECT_EQ(reached.size(), kOrientationCount);
  EXPECT_EQ(QuarterTurn(Orientation {}, TurnAxis::kZ), Orientation::kFacePosYUpPosZ);
}

// A wall panel: stretches along its own X and Z, one cell thick.
const z13::station::BlockSpec kPanel {.type_id = 2, .size = {8, 1, 8}};
const Eigen::Vector3i kPanelMin {1, 1, 1};
const Eigen::Vector3i kPanelMax {256, 1, 256};

TEST(PlacementTest, ADragSpansTheCellsItMovedAndKeepsTheBrushSizeElsewhere) {
  const Block forward = DraggedBlock({0, 0, 0}, {5, 0, 0}, kPanel, kPanelMin, kPanelMax);
  EXPECT_EQ(forward.spec.size, Eigen::Vector3i(5, 1, 8));
  EXPECT_EQ(forward.cell, Eigen::Vector3i(0, 0, -4));

  const Block backward = DraggedBlock({0, 0, 0}, {-3, 0, 0}, kPanel, kPanelMin, kPanelMax);
  EXPECT_EQ(backward.spec.size, Eigen::Vector3i(3, 1, 8));
  EXPECT_EQ(OccupiedCells(backward).End().x(), 1);

  const Block click = DraggedBlock({0, 0, 0}, {0, 0, 0}, kPanel, kPanelMin, kPanelMax);
  EXPECT_EQ(click.spec.size, kPanel.size);
}

TEST(PlacementTest, ADragStaysWithinTheSizeLimits) {
  const Block thick = DraggedBlock({0, 0, 0}, {0, 4, 0}, kPanel, kPanelMin, kPanelMax);
  EXPECT_EQ(thick.spec.size, Eigen::Vector3i(8, 1, 8));

  const z13::station::BlockSpec door {.type_id = 3, .size = {6, 1, 10}};
  const Block fixed = DraggedBlock({0, 0, 0}, {20, 0, 20}, door, door.size, door.size);
  EXPECT_EQ(fixed.spec.size, door.size);
  EXPECT_EQ(fixed.cell, Eigen::Vector3i(0, 0, 0));
}

// Turned to face +Y, the panel's own X runs along the world Y the drag moves along.
TEST(PlacementTest, ADragSizesATurnedBrushAlongItsOwnAxes) {
  const z13::station::BlockSpec turned {.type_id = 2, .size = {8, 1, 8}, .orientation = Orientation::kFacePosYUpPosZ};
  const Block block = DraggedBlock({0, 0, 0}, {0, 6, 0}, turned, kPanelMin, kPanelMax);
  EXPECT_EQ(block.spec.size, Eigen::Vector3i(6, 1, 8));
  EXPECT_EQ(OccupiedCells(block).extent, Eigen::Vector3i(1, 6, 8));
}

TEST(PlacementTest, CellBoxesOverlapOnlyWhenTheyShareACell) {
  const CellBox box {.min = {0, 0, 0}, .extent = {2, 2, 2}};
  EXPECT_TRUE(box.Overlaps({.min = {1, 1, 1}, .extent = {2, 2, 2}}));
  EXPECT_FALSE(box.Overlaps({.min = {2, 0, 0}, .extent = {2, 2, 2}}));
  EXPECT_TRUE(box.Contains({1, 1, 1}));
  EXPECT_FALSE(box.Contains({2, 1, 1}));
}

}  // namespace
}  // namespace z13::building::primitives
