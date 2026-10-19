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
#include <map>
#include <utility>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <primitives/occupancy.h>
#include <primitives/placement.h>
#include <primitives/wall_seam.h>
#include <z13_tests/shipped_station.h>

namespace z13::building::primitives {
namespace {

using z13::station::WallAxis;

constexpr int kReach = 64;

PlanWall Wall(const Eigen::Vector2i& direction, const Eigen::Vector2i& offset) {
  return {.node = Eigen::Vector2i::Zero(), .direction = direction, .offset = offset};
}

PlanPoint FarEnd(const PlanWall& wall) {
  return PlanPoint(wall.direction.x(), wall.direction.y(), 1);
}

// A point the tests know is on a whole cell.
Eigen::Vector2i Whole(const PlanPoint& point) {
  EXPECT_EQ(point.x() % point.z(), 0);
  EXPECT_EQ(point.y() % point.z(), 0);
  return {static_cast<int>(point.x() / point.z()), static_cast<int>(point.y() / point.z())};
}

TEST(WallSeamTest, TwoWallsThickeningIntoTheirCornerAreMitredAcrossIt) {
  const auto seam = PlanSeam(Wall({10, 0}, {0, 2}), Wall({0, 10}, {2, 0}), kReach);
  ASSERT_TRUE(seam.has_value()) << seam.error();
  EXPECT_EQ(Whole(seam->outer), Eigen::Vector2i(0, 0));
  EXPECT_EQ(Whole(seam->inner), Eigen::Vector2i(2, 2));
  EXPECT_EQ(seam->line, SeamLine(1, -1, 0));
}

TEST(WallSeamTest, WallsOnTheTwoSidesOfTheirPathMeetAcrossAnExtendedEnd) {
  const PlanWall first = Wall({10, 0}, {0, 2});
  const PlanWall second = Wall({0, 10}, {-2, 0});
  const auto seam = PlanSeam(first, second, kReach);
  ASSERT_TRUE(seam.has_value()) << seam.error();
  EXPECT_EQ(Whole(seam->inner), Eigen::Vector2i(0, 2));
  EXPECT_EQ(Whole(seam->outer), Eigen::Vector2i(-2, 0));
  EXPECT_EQ(seam->line, SeamLine(1, -1, 2));
  // The first wall keeps the cell behind its node.
  EXPECT_EQ(SideOf(seam->line, PlanPoint(-1, 0, 1)), SideOf(seam->line, FarEnd(first)));
}

TEST(WallSeamTest, TheSeamIsTheSameFromEitherWall) {
  const PlanWall first = Wall({12, 5}, {0, 3});
  const PlanWall second = Wall({-4, 9}, {2, 0});
  const auto forward = PlanSeam(first, second, kReach);
  const auto backward = PlanSeam(second, first, kReach);
  ASSERT_TRUE(forward.has_value()) << forward.error();
  ASSERT_TRUE(backward.has_value()) << backward.error();
  EXPECT_EQ(forward->line, backward->line);
}

TEST(WallSeamTest, TheWallsKeepOppositeSidesAndTheSeamHoldsBothCorners) {
  for (const Eigen::Vector2i& first_offset : {Eigen::Vector2i(0, 3), Eigen::Vector2i(0, -3)}) {
    for (const Eigen::Vector2i& second_offset : {Eigen::Vector2i(2, 0), Eigen::Vector2i(-2, 0)}) {
      const PlanWall first = Wall({12, 5}, first_offset);
      const PlanWall second = Wall({-4, 9}, second_offset);
      const auto seam = PlanSeam(first, second, kReach);
      ASSERT_TRUE(seam.has_value()) << seam.error();
      EXPECT_EQ(SideOf(seam->line, seam->inner), 0);
      EXPECT_EQ(SideOf(seam->line, seam->outer), 0);
      EXPECT_LT(SideOf(seam->line, FarEnd(first)) * SideOf(seam->line, FarEnd(second)), 0);
    }
  }
}

TEST(WallSeamTest, AStraightRunEndsAcrossItsThicknessAtTheNode) {
  const auto seam = PlanSeam(Wall({10, 0}, {0, 2}), Wall({-10, 0}, {0, 2}), kReach);
  ASSERT_TRUE(seam.has_value()) << seam.error();
  EXPECT_EQ(seam->line, SeamLine(1, 0, 0));
}

TEST(WallSeamTest, WallsLeavingTheNodeTheSameWayHaveNoSeam) {
  EXPECT_FALSE(PlanSeam(Wall({10, 0}, {0, 2}), Wall({6, 0}, {0, -1}), kReach).has_value());
}

TEST(WallSeamTest, AWallOfNoLengthOrThicknessHasNoSeam) {
  EXPECT_FALSE(PlanSeam(Wall({0, 0}, {0, 2}), Wall({0, 10}, {2, 0}), kReach).has_value());
  EXPECT_FALSE(PlanSeam(Wall({10, 0}, {0, 0}), Wall({0, 10}, {2, 0}), kReach).has_value());
  EXPECT_FALSE(PlanSeam(Wall({1000, 0}, {0, 2}), Wall({0, 10}, {2, 0}), kReach).has_value());
}

TEST(WallSeamTest, ATipReachingTooFarIsRefused) {
  const PlanWall first = Wall({20, 0}, {0, 2});
  const PlanWall second = Wall({20, 3}, {0, -2});
  const auto seam = PlanSeam(first, second, 8);
  ASSERT_FALSE(seam.has_value());
  EXPECT_NE(seam.error().find("reach"), std::string::npos) << seam.error();
}

TEST(WallSeamTest, AWallTooShortForTheJointSaysHowLongItMustBe) {
  const PlanWall first = Wall({20, 0}, {0, 2});
  const PlanWall second = Wall({20, 3}, {0, -2});
  const auto seam = PlanSeam(first, second, kReach);
  ASSERT_FALSE(seam.has_value());
  // The tip is 80/3 = 26.7 cells along: 27 are taken, plus the cell the wall keeps.
  EXPECT_NE(seam.error().find("at least 28"), std::string::npos) << seam.error();
  EXPECT_TRUE(PlanSeam(Wall({29, 0}, {0, 2}), Wall({35, 5}, {0, -2}), kReach).has_value());
}

TEST(WallSeamTest, AWallComesAsItsEndAtANodeInThePlaneOfItsStick) {
  const z13::building::primitives::WallGeometry wall {
      .node_a = {0, 0, 0}, .node_b = {7, 3, 0}, .stick_axis = WallAxis::kZ, .stick_length = 5, .thickness = 2};
  const auto at_a = PlanWallAt(wall, true);
  const auto at_b = PlanWallAt(wall, false);
  ASSERT_TRUE(at_a.has_value());
  ASSERT_TRUE(at_b.has_value());
  EXPECT_EQ(at_a->node, Eigen::Vector2i(0, 0));
  EXPECT_EQ(at_a->direction, Eigen::Vector2i(7, 3));
  EXPECT_EQ(at_a->offset, Eigen::Vector2i(0, 2));
  EXPECT_EQ(at_b->node, Eigen::Vector2i(7, 3));
  EXPECT_EQ(at_b->direction, Eigen::Vector2i(-7, -3));
  EXPECT_EQ(at_b->offset, Eigen::Vector2i(0, 2));

  // A stick along X puts the plane at (y, z).
  const z13::building::primitives::WallGeometry upright {
      .node_a = {4, 0, 0}, .node_b = {4, 3, 7}, .stick_axis = WallAxis::kX, .stick_length = 5, .thickness = -1};
  const auto in_yz = PlanWallAt(upright, true);
  ASSERT_TRUE(in_yz.has_value());
  EXPECT_EQ(in_yz->direction, Eigen::Vector2i(3, 7));
  EXPECT_EQ(in_yz->offset, Eigen::Vector2i(-1, 0));
}

WallSeam SeamAt(const WallGeometry& first, const WallGeometry& second) {
  const auto seam = PlanSeam(*PlanWallAt(first, true), *PlanWallAt(second, true), kReach);
  EXPECT_TRUE(seam.has_value()) << (seam ? "" : seam.error());
  return seam.value_or(WallSeam {});
}

// The section's four corners of a panel with cut ends, in the plane (x, y) of a Z stick.
std::vector<Eigen::Vector2i> WorldCorners(const z13::station::Block& block) {
  const CellPose pose = PoseOf(block);
  std::vector<Eigen::Vector2i> corners;
  for (size_t i = 0; i < 4; ++i) {
    const Eigen::Vector3i world =
        pose.origin + pose.rotation * Eigen::Vector3i(block.spec.corners[2 * i], 0, block.spec.corners[(2 * i) + 1]);
    corners.emplace_back(world.x(), world.y());
  }
  return corners;
}

const Palette& ShippedPalette() {
  static const Palette palette = z13::testing::ShippedPalette().value();
  return palette;
}

TEST(WallJointTest, TwoWallsMitredAtACornerCoverItsCellsOnceExceptOnTheSeam) {
  const WallGeometry across {
      .node_a = {0, 0, 0}, .node_b = {10, 0, 0}, .stick_axis = WallAxis::kZ, .stick_length = 4, .thickness = 2};
  const WallGeometry up {
      .node_a = {0, 0, 0}, .node_b = {0, 10, 0}, .stick_axis = WallAxis::kZ, .stick_length = 4, .thickness = 2};
  const WallSeam seam = SeamAt(across, up);
  const auto first = WallPanelBlock(across, ShippedPalette(), {.at_a = seam});
  const auto second = WallPanelBlock(up, ShippedPalette(), {.at_a = seam});
  ASSERT_TRUE(first.has_value()) << first.error();
  ASSERT_TRUE(second.has_value()) << second.error();

  std::map<std::pair<int, int>, int> covered;
  for (const auto& block : {*first, *second}) {
    for (const CellBox& box : SealedBoxes(block, ShippedPalette())) {
      for (int x = box.min.x(); x < box.End().x(); ++x) {
        for (int y = box.min.y(); y < box.End().y(); ++y) {
          ++covered[{x, y}];
        }
      }
    }
  }
  std::map<std::pair<int, int>, int> expected;
  for (int x = 0; x < 10; ++x) {
    for (int y = 0; y < 10; ++y) {
      if (y < 2 || x < 2) {
        expected[{x, y}] = (x == y && x < 2) ? 2 : 1;
      }
    }
  }
  EXPECT_EQ(covered, expected);
}

TEST(WallJointTest, TheCornersOfTwoObliqueWallsMeetOnTheSameSeam) {
  const WallGeometry first {
      .node_a = {0, 0, 0}, .node_b = {12, 5, 0}, .stick_axis = WallAxis::kZ, .stick_length = 4, .thickness = 3};
  const WallGeometry second {
      .node_a = {0, 0, 0}, .node_b = {-4, 9, 0}, .stick_axis = WallAxis::kZ, .stick_length = 4, .thickness = 2};
  const WallSeam seam = SeamAt(first, second);
  const auto a = WallPanelBlock(first, ShippedPalette(), {.at_a = seam});
  const auto b = WallPanelBlock(second, ShippedPalette(), {.at_a = seam});
  ASSERT_TRUE(a.has_value()) << a.error();
  ASSERT_TRUE(b.has_value()) << b.error();

  // Reference face near, thickened face near: the two ends of the seam.
  const auto seam_ends = [](const z13::station::Block& block) {
    const std::vector<Eigen::Vector2i> corners = WorldCorners(block);
    std::vector<std::pair<int, int>> ends {{corners[0].x(), corners[0].y()}, {corners[3].x(), corners[3].y()}};
    std::ranges::sort(ends);
    return ends;
  };
  EXPECT_EQ(seam_ends(*a), seam_ends(*b));
}

TEST(WallJointTest, AWallWithoutSeamsStaysSquareCut) {
  const WallGeometry wall {
      .node_a = {0, 0, 0}, .node_b = {12, 5, 0}, .stick_axis = WallAxis::kZ, .stick_length = 4, .thickness = 2};
  const auto plain = WallPanelBlock(wall, ShippedPalette());
  const auto same = WallPanelBlock(wall, ShippedPalette(), {});
  ASSERT_TRUE(plain.has_value());
  ASSERT_TRUE(same.has_value());
  EXPECT_EQ(*plain, *same);
  EXPECT_FALSE(plain->spec.HasCorners());
}

}  // namespace
}  // namespace z13::building::primitives
