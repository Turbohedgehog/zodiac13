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

namespace z13::primitives {
namespace {

TEST(PlacementTest, TheTwentyFourOrientationsAreDistinctRotations) {
  std::vector<Eigen::Matrix3i> seen;
  for (uint8_t orientation = 0; orientation < kOrientationCount; ++orientation) {
    const Eigen::Matrix3i m = OrientationMatrix(orientation);
    EXPECT_EQ(m * m.transpose(), Eigen::Matrix3i::Identity());
    EXPECT_EQ(m.cast<float>().determinant(), 1.f);
    for (const Eigen::Matrix3i& other : seen) {
      EXPECT_NE(m, other) << static_cast<int>(orientation);
    }
    seen.push_back(m);
  }
  EXPECT_EQ(OrientationMatrix(0), Eigen::Matrix3i::Identity());
}

TEST(PlacementTest, InverseOrientationUndoesTheRotation) {
  for (uint8_t orientation = 0; orientation < kOrientationCount; ++orientation) {
    const uint8_t inverse = InverseOrientation(orientation);
    EXPECT_EQ(OrientationMatrix(inverse) * OrientationMatrix(orientation), Eigen::Matrix3i::Identity());
    EXPECT_EQ(InverseOrientation(inverse), orientation);
  }
}

// The posed frame [0, size] must cover exactly the occupied cells, whatever the turn.
TEST(PlacementTest, PoseMapsTheFrameOntoTheOccupiedCells) {
  for (uint8_t orientation = 0; orientation < kOrientationCount; ++orientation) {
    const Placement placement {.cell = {3, -2, 5}, .size = {4, 1, 7}, .orientation = orientation};
    const CellPose pose = placement.Pose();
    const CellBox occupied = placement.Occupied();
    Eigen::Vector3i low = Eigen::Vector3i::Constant(INT32_MAX);
    Eigen::Vector3i high = Eigen::Vector3i::Constant(INT32_MIN);
    for (int corner = 0; corner < 8; ++corner) {
      const Eigen::Vector3i local((corner & 1) ? placement.size.x() : 0, (corner & 2) ? placement.size.y() : 0,
                                  (corner & 4) ? placement.size.z() : 0);
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
  uint8_t quarter_turn = 0;
  for (uint8_t orientation = 0; orientation < kOrientationCount; ++orientation) {
    const Eigen::Matrix3i m = OrientationMatrix(orientation);
    if (m(2, 2) == 1 && m(1, 0) == 1) {
      quarter_turn = orientation;
    }
  }
  ASSERT_NE(quarter_turn, 0);

  const Placement placement = PlaceCentredOn({10.2f, 0.9f, -0.4f}, {4, 2, 1}, quarter_turn);
  EXPECT_EQ(placement.Occupied().extent, Eigen::Vector3i(2, 4, 1));
  EXPECT_EQ(placement.cell, Eigen::Vector3i(9, -1, -1));
}

TEST(PlacementTest, CellBoxesOverlapOnlyWhenTheyShareACell) {
  const CellBox box {.min = {0, 0, 0}, .extent = {2, 2, 2}};
  EXPECT_TRUE(box.Overlaps({.min = {1, 1, 1}, .extent = {2, 2, 2}}));
  EXPECT_FALSE(box.Overlaps({.min = {2, 0, 0}, .extent = {2, 2, 2}}));
  EXPECT_TRUE(box.Contains({1, 1, 1}));
  EXPECT_FALSE(box.Contains({2, 1, 1}));
}

}  // namespace
}  // namespace z13::primitives
