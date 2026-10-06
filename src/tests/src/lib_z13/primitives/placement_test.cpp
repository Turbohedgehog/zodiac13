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

TEST(PlacementTest, CellBoxesOverlapOnlyWhenTheyShareACell) {
  const CellBox box {.min = {0, 0, 0}, .extent = {2, 2, 2}};
  EXPECT_TRUE(box.Overlaps({.min = {1, 1, 1}, .extent = {2, 2, 2}}));
  EXPECT_FALSE(box.Overlaps({.min = {2, 0, 0}, .extent = {2, 2, 2}}));
  EXPECT_TRUE(box.Contains({1, 1, 1}));
  EXPECT_FALSE(box.Contains({2, 1, 1}));
}

}  // namespace
}  // namespace z13::building::primitives
