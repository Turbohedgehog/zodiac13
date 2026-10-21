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
#include <array>
#include <optional>
#include <set>
#include <vector>

#include <Eigen/Dense>

#include <primitives/geometry.h>
#include <primitives/occupancy.h>
#include <primitives/placement.h>
#include <primitives/wall_panel.h>
#include <primitives/wall_seam.h>
#include <z13_tests/shipped_station.h>

namespace z13::building::primitives {
namespace {

using z13::station::Block;
using z13::station::WallAxis;

const Palette& ShippedPalette() {
  static const Palette palette = z13::testing::ShippedPalette().value();
  return palette;
}

// A wall from the origin to (7, 3) in plan, 5 tall.
WallGeometry PlanWall(int thickness) {
  return {.node_b = {7, 3, 0}, .stick_axis = WallAxis::kZ, .stick_length = 5, .thickness = thickness};
}

TEST(WallPanelBlockTest, AWallOfAnyDirectionFillsTheStripBetweenItsLines) {
  const Block block = WallPanelBlock(PlanWall(2), ShippedPalette()).value();
  const std::vector<CellBox> sealed = SealedBoxes(block, ShippedPalette());
  const auto in_strip = [&sealed](const Eigen::Vector3i& cell) {
    return std::ranges::any_of(sealed, [&cell](const CellBox& box) { return box.Contains(cell); });
  };

  // The strip runs y in [3x/7, 3x/7 + 2]: a cell is sealed when its centre is in it.
  EXPECT_EQ(OccupiedCells(block).min, Eigen::Vector3i(0, 0, 0));
  EXPECT_EQ(OccupiedCells(block).extent, Eigen::Vector3i(7, 5, 5));
  for (int x = 0; x < 7; ++x) {
    for (int y = 0; y < 5; ++y) {
      const double centre_y = y + 0.5;
      const double low = 3.0 * (x + 0.5) / 7.0;
      EXPECT_EQ(in_strip({x, y, 2}), centre_y >= low && centre_y <= low + 2) << x << ',' << y;
    }
  }
  EXPECT_TRUE(in_strip({3, 1, 4}));
}

TEST(WallPanelBlockTest, EveryDirectionAndSideGivesAPanelThatFitsTheTargetCorners) {
  for (const WallAxis axis : {WallAxis::kX, WallAxis::kY, WallAxis::kZ}) {
    for (const int dx : {-7, 0, 5}) {
      for (const int dy : {-3, 0, 4}) {
        for (const int thickness : {-3, 3}) {
          for (const int length : {-4, 6}) {
            const WallGeometry wall {
                .node_a = {10, -20, 5}, .node_b = Eigen::Vector3i(10, -20, 5) + Eigen::Vector3i(dx, dy, 0),
                .stick_axis = axis, .stick_length = length, .thickness = thickness};
            const Eigen::Vector3i offset = wall.node_b - wall.node_a;
            Eigen::Vector3i plane = offset;
            plane[static_cast<int>(axis)] = 0;
            const auto block = WallPanelBlock(wall, ShippedPalette());
            if (plane.isZero()) {
              EXPECT_FALSE(block.has_value());
              continue;
            }
            ASSERT_TRUE(block.has_value()) << block.error();
            // The prism's footprint covers both ends of the line and the stick over it.
            const CellBox cells = OccupiedCells(*block);
            const std::array<Eigen::Vector3i, 2> ends {wall.node_a, wall.node_a + plane};
            for (const Eigen::Vector3i& end : ends) {
              for (int axis_index = 0; axis_index < 3; ++axis_index) {
                const bool along = axis_index == static_cast<int>(axis);
                const int stick_end = end[axis_index] + (along ? length : 0);
                EXPECT_GE(std::min(end[axis_index], stick_end), cells.min[axis_index]);
                EXPECT_LE(std::max(end[axis_index], stick_end), cells.End()[axis_index]);
              }
            }
          }
        }
      }
    }
  }
}

// The corners of the wall's footprint with each stick over them, as the plan puts them.
std::set<std::array<int, 3>> TargetCorners(const WallGeometry& wall) {
  const int stick = static_cast<int>(wall.stick_axis);
  Eigen::Vector3i plane = wall.node_b - wall.node_a;
  plane[stick] = 0;
  const PlanAxes axes = PlanAxesOf(wall.stick_axis, plane);
  Eigen::Vector3i thicken = Eigen::Vector3i::Zero();
  thicken[axes.minor] = wall.thickness;
  std::set<std::array<int, 3>> corners;
  for (const bool at_b : {false, true}) {
    for (const Eigen::Vector3i& side : {Eigen::Vector3i(Eigen::Vector3i::Zero()), thicken}) {
      const Eigen::Vector3i base = wall.node_a + (at_b ? plane : Eigen::Vector3i::Zero()) + side;
      Eigen::Vector3i top = base;
      top[stick] += wall.stick_length + (at_b ? wall.stick_growth : 0);
      corners.insert({base.x(), base.y(), base.z()});
      corners.insert({top.x(), top.y(), top.z()});
    }
  }
  return corners;
}

std::set<std::array<int, 3>> PlacedCorners(const Block& block) {
  const CellPose pose = PoseOf(block);
  std::set<std::array<int, 3>> placed;
  for (const ConvexSolid& solid : BlockSolids(ShippedPalette(), ShapeKeyOf(block.spec))) {
    for (const Eigen::Vector3f& vertex : solid.vertices) {
      const Eigen::Vector3i corner = pose.origin + pose.rotation * vertex.array().round().cast<int>().matrix();
      placed.insert({corner.x(), corner.y(), corner.z()});
    }
  }
  return placed;
}

TEST(WallPanelBlockTest, ATrapezoidFitsItsCornersInEveryDirectionSideAndSlope) {
  constexpr int kRun = 8;
  constexpr int kRise = 3;
  for (const WallAxis axis : {WallAxis::kX, WallAxis::kY, WallAxis::kZ}) {
    for (const int dx : {-kRun, kRun}) {
      for (const int dy : {-kRise, 0, kRise}) {
        for (const int thickness : {-3, 3}) {
          for (const int length : {-6, 6}) {
            for (const int growth : {-2, 2}) {
              const WallGeometry wall {
                  .node_a = {10, -20, 5}, .node_b = Eigen::Vector3i(10, -20, 5) + Eigen::Vector3i(dx, dy, 0),
                  .stick_axis = axis, .stick_length = length, .stick_growth = growth * (length > 0 ? 1 : -1),
                  .thickness = thickness};
              Eigen::Vector3i plane = wall.node_b - wall.node_a;
              plane[static_cast<int>(axis)] = 0;
              if (plane.isZero()) {
                continue;
              }
              const auto block = WallPanelBlock(wall, ShippedPalette());
              ASSERT_TRUE(block.has_value()) << block.error();
              EXPECT_EQ(PlacedCorners(*block), TargetCorners(wall));
            }
          }
        }
      }
    }
  }
}

TEST(WallPanelBlockTest, ATrapezoidIsSealedUnderItsSlopedTop) {
  const WallGeometry wall {
      .node_b = {10, 0, 0}, .stick_axis = WallAxis::kZ, .stick_length = 8, .stick_growth = -4, .thickness = 1};
  const Block block = WallPanelBlock(wall, ShippedPalette()).value();
  const std::vector<CellBox> sealed = SealedBoxes(block, ShippedPalette());

  for (int x = 0; x < 10; ++x) {
    for (int z = 0; z < 8; ++z) {
      const bool inside = std::ranges::any_of(sealed, [&](const CellBox& box) { return box.Contains({x, 0, z}); });
      EXPECT_EQ(inside, z + 0.5 <= 8.0 - 4.0 * (x + 0.5) / 10.0) << x << ',' << z;
    }
  }
}

TEST(WallPanelBlockTest, SticksPointingOppositeWaysAreRefused) {
  EXPECT_FALSE(WallPanelBlock({.node_b = {10, 0, 0}, .stick_length = 4, .stick_growth = -4, .thickness = 1},
                              ShippedPalette()).has_value());
  EXPECT_FALSE(WallPanelBlock({.node_b = {10, 0, 0}, .stick_length = 4, .stick_growth = -6, .thickness = 1},
                              ShippedPalette()).has_value());
}

TEST(WallPanelBlockTest, ATrapezoidCutByASeamKeepsItsSlopeOverTheMovedCorners) {
  constexpr int kReach = 16;
  constexpr int kLength = 12;
  constexpr int kStick = 6;
  constexpr int kShrink = 3;
  const WallGeometry east {
      .node_b = {kLength, 0, 0}, .stick_axis = WallAxis::kZ, .stick_length = kStick, .thickness = 2};
  const WallGeometry north {.node_b = {0, kLength, 0}, .stick_axis = WallAxis::kZ, .stick_length = kStick,
                            .stick_growth = -kShrink, .thickness = 2};
  const auto seam = PlanSeam(PlanWallAt(east, true).value(), PlanWallAt(north, true).value(), kReach);
  ASSERT_TRUE(seam.has_value()) << seam.error();

  const auto block = WallPanelBlock(north, ShippedPalette(), {.at_a = *seam});

  ASSERT_TRUE(block.has_value()) << block.error();
  EXPECT_TRUE(block->spec.HasCorners());
  EXPECT_TRUE(block->spec.HasFaceCuts());
  std::vector<Eigen::Vector3i> vertices;
  const CellPose pose = PoseOf(*block);
  for (const ConvexSolid& solid : BlockSolids(ShippedPalette(), ShapeKeyOf(block->spec))) {
    for (const Eigen::Vector3f& vertex : solid.vertices) {
      vertices.push_back(pose.origin + pose.rotation * vertex.array().round().cast<int>().matrix());
    }
  }
  for (const Eigen::Vector3i& vertex : vertices) {
    int top = vertex.z();
    int bottom = vertex.z();
    for (const Eigen::Vector3i& other : vertices) {
      if (other.x() == vertex.x() && other.y() == vertex.y()) {
        top = std::max(top, other.z());
        bottom = std::min(bottom, other.z());
      }
    }
    EXPECT_EQ(bottom, 0);
    EXPECT_NEAR(top, kStick - static_cast<double>(kShrink) * vertex.y() / kLength, 0.5) << vertex.y();
  }
}

TEST(WallPanelBlockTest, AWallThinnerThanTheSealRuleIsRefused) {
  EXPECT_FALSE(WallPanelBlock(PlanWall(1), ShippedPalette()).has_value());
  EXPECT_TRUE(WallPanelBlock(PlanWall(2), ShippedPalette()).has_value());
  EXPECT_TRUE(WallPanelBlock(PlanWall(-2), ShippedPalette()).has_value());
}

TEST(WallPanelBlockTest, AnAxisWallIsAThickSlabFlushWithTheGrid) {
  const WallGeometry wall {.node_b = {12, 0, 0}, .stick_length = 8, .thickness = 1};

  const Block block = WallPanelBlock(wall, ShippedPalette()).value();

  EXPECT_EQ(block.spec.slant, 0);
  EXPECT_EQ(OccupiedCells(block).min, Eigen::Vector3i(0, 0, 0));
  EXPECT_EQ(OccupiedCells(block).extent, Eigen::Vector3i(12, 1, 8));
  EXPECT_FALSE(ShapeOfBlock(block, ShippedPalette()).filled.has_value());
}

TEST(WallPanelBlockTest, BadSegmentsAreRefused) {
  EXPECT_FALSE(WallPanelBlock({.node_a = {1, 2, 3}, .node_b = {1, 2, 9}, .stick_length = 4, .thickness = 1},
                              ShippedPalette()).has_value()) << "the nodes differ only along the stick";
  EXPECT_FALSE(WallPanelBlock({.node_b = {5, 0, 0}, .stick_length = 0, .thickness = 1}, ShippedPalette()).has_value());
  EXPECT_FALSE(WallPanelBlock({.node_b = {5, 0, 0}, .stick_length = 4, .thickness = 0}, ShippedPalette()).has_value());
}

TEST(WallPanelBlockTest, AnObliqueWallNeedsTwoCellsToHoldAirBack) {
  WallGeometry wall {.node_a = {0, 0, 0}, .node_b = {10, 0, 0}, .stick_axis = WallAxis::kZ, .stick_length = 3};
  EXPECT_EQ(MinWallThickness(wall), 1);
  for (const Eigen::Vector3i& node_b : {Eigen::Vector3i(10, 1, 0), Eigen::Vector3i(7, 3, 0), Eigen::Vector3i(10, 10, 0),
                                        Eigen::Vector3i(-4, 9, 0)}) {
    wall.node_b = node_b;
    const std::optional<int> thickness = MinWallThickness(wall);
    ASSERT_TRUE(thickness.has_value());
    EXPECT_EQ(*thickness, 2);
    wall.thickness = *thickness;
    EXPECT_TRUE(WallPanelBlock(wall, ShippedPalette()).has_value());
    wall.thickness = *thickness - 1;
    EXPECT_FALSE(WallPanelBlock(wall, ShippedPalette()).has_value());
  }
  wall.node_b = wall.node_a;
  EXPECT_FALSE(MinWallThickness(wall).has_value());
}

}  // namespace
}  // namespace z13::building::primitives
